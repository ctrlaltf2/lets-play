use anyhow::Context;
use cudarc::{
    driver::{
        sys::{CUdeviceptr, CUmemorytype},
        CudaDevice, CudaSlice, DevicePtr, LaunchAsync,
    },
    nvrtc::CompileOptions,
};
use letsplay_gpu::egl_helpers::DeviceContext;
use std::{
    sync::{Arc, Condvar, Mutex, MutexGuard},
    time::Duration,
};
use tokio::sync::mpsc::{self, error::TryRecvError};

use crate::h264_encoder::H264Encoder;
use crate::{cuda_gl::safe::GraphicsResource, ffmpeg};

use letsplay_core::Size;

use super::EncoderCommand;
use super::EncoderThreadControl;

struct EncoderStateHW {
    encoder: Option<H264Encoder>,
    frame: ffmpeg::frame::Video,
    packet: ffmpeg::Packet,
}

impl EncoderStateHW {
    fn new() -> Self {
        Self {
            encoder: None,
            frame: ffmpeg::frame::Video::empty(),
            packet: ffmpeg::Packet::empty(),
        }
    }

    fn init(&mut self, device: &Arc<CudaDevice>, size: Size) -> anyhow::Result<()> {
        self.encoder = Some(H264Encoder::new_nvenc_hwframe(
            &device,
            size.clone(),
            60,
            2 * (1024 * 1024),
        )?);

        // replace packet
        self.packet = ffmpeg::Packet::empty();
        self.frame = self.encoder.as_mut().unwrap().create_frame()?;

        Ok(())
    }

    #[inline]
    fn frame(&mut self) -> &mut ffmpeg::frame::Video {
        &mut self.frame
    }

    fn send_frame(&mut self, pts: u64, force_keyframe: bool) -> Option<ffmpeg::Packet> {
        let frame = &mut self.frame;
        let encoder = self.encoder.as_mut().unwrap();

        // set frame metadata
        unsafe {
            if force_keyframe {
                (*frame.as_mut_ptr()).pict_type = ffmpeg::sys::AVPictureType::AV_PICTURE_TYPE_I;
                (*frame.as_mut_ptr()).flags = ffmpeg::sys::AV_FRAME_FLAG_KEY;
                (*frame.as_mut_ptr()).key_frame = 1;
            } else {
                (*frame.as_mut_ptr()).pict_type = ffmpeg::sys::AVPictureType::AV_PICTURE_TYPE_P;
                (*frame.as_mut_ptr()).flags = 0i32;
                (*frame.as_mut_ptr()).key_frame = 0;
            }

            (*frame.as_mut_ptr()).pts = pts as i64;
        }

        encoder.send_frame(&*frame);
        encoder
            .receive_packet(&mut self.packet)
            .expect("Failed to recieve packet");

		// This looks sketchy (and sad, due to Clone::clone()), but
		// Packet really is reference-counted (internally, deep in the pits of ffmpeg).
		// So this just clones a pointer. I probably could have just said that.
        unsafe {
            if !self.packet.is_empty() {
                return Some(self.packet.clone());
            }
        }

        return None;
    }
}

/// Source for the kernel used to flip OpenGL framebuffers right-side up.
const OPENGL_FLIP_KERNEL_SRC: &str = "
extern \"C\" __global__ void flip_opengl(
    const unsigned* pSrc,
    unsigned* pDest,
    int width, 
    int height
) {
    const unsigned x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height) {
        unsigned reversed_y = (height - 1) - y;
        ((unsigned*)pDest)[y * width + x] = ((unsigned*)pSrc)[reversed_y * width + x];
    }
}";

/// Version which also replaces channel order
const OPENGL_FLIP_KERNEL_BGRA_SRC: &str = "
extern \"C\" __global__ void flip_opengl(
    const unsigned* pSrc,
    unsigned* pDest,
    int width, 
    int height
) {
    const unsigned x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned y = blockIdx.y * blockDim.y + threadIdx.y;

    // byte pointers for channel swap
    const unsigned char* pSrcData = (const unsigned char*)pSrc;
    unsigned char* pBufferData = (unsigned char*)pDest;

    if (x < width && y < height) {
        //unsigned reversed_y = (height - 1) - y;
        // aaa
        unsigned long long srcStart = (y * width);
		unsigned long long dstStart = (y * width);

        // swap. probably should do this in a way that does more than 1 pixel at a time but oh well
        pBufferData[((dstStart + x) * 4) + 0] = pSrcData[((srcStart + x) * 4) + 2]; // B
        pBufferData[((dstStart + x) * 4) + 1] = pSrcData[((srcStart + x) * 4) + 1]; // G
		pBufferData[((dstStart + x) * 4) + 2] = pSrcData[((srcStart + x) * 4) + 0]; // R
		pBufferData[((dstStart + x) * 4) + 3] = 0xff;							    // A
    }
}";

fn encoder_thread_hwframe_main(
    input_msg_notify: Arc<Condvar>,
    input_msg: Arc<Mutex<EncoderCommand>>,

    processed_notify: Arc<Condvar>,
    processed: Arc<Mutex<bool>>,

    packet_update: Arc<Condvar>,
    packet: Arc<Mutex<ffmpeg::Packet>>,

    rgba_to_bgra_kernel: bool,

    cuda_device: &Arc<CudaDevice>,
    cuda_resource: &Arc<Mutex<GraphicsResource>>,
    gl_context: &Arc<Mutex<DeviceContext>>,
) -> anyhow::Result<()> {
    let mut frame_number = 0u64;
    let mut force_keyframe = false;

    let mut encoder = EncoderStateHW::new();

    // :)
    cuda_device.bind_to_thread()?;

    // Compile the given support kernel.
    let ptx = cudarc::nvrtc::compile_ptx_with_opts(
        if rgba_to_bgra_kernel {
            &OPENGL_FLIP_KERNEL_BGRA_SRC
        } else {
            &OPENGL_FLIP_KERNEL_SRC
        },
        CompileOptions {
            //options: vec!["--gpu-architecture=compute_50".into()],
            ..Default::default()
        },
    )
    .with_context(|| "compiling support kernel")?;

    // pop it in
    cuda_device.load_ptx(ptx, "module", &["flip_opengl"])?;

    let mut memcpy = cudarc::driver::sys::CUDA_MEMCPY2D_st::default();

    // setup the things that won't change about the cuda memcpy

    // src
    memcpy.srcXInBytes = 0;
    memcpy.srcY = 0;
    memcpy.srcMemoryType = CUmemorytype::CU_MEMORYTYPE_ARRAY;

    // dest
    memcpy.dstXInBytes = 0;
    memcpy.dstY = 0;
    memcpy.dstMemoryType = CUmemorytype::CU_MEMORYTYPE_DEVICE;

    // Temporary buffer used for opengl flip on the GPU. We copy to this buffer,
    // then copy the flipped version (using the launched support kernel) to the CUDA device memory ffmpeg
    // allocated.
    let mut temp_buffer: CudaSlice<u32> = cuda_device.alloc_zeros::<u32>(48).expect("over");

    loop {
        // wait for a message
        {
            let lk = input_msg.lock().expect("???");
            let waited_lk = input_msg_notify.wait(lk).expect("you bone");

            match &*waited_lk {
                EncoderCommand::Init { size } => {
                    frame_number = 0;
                    force_keyframe = true;

                    // Allocate the flip buffer
                    temp_buffer = cuda_device
                        .alloc_zeros::<u32>((size.width * size.height) as usize)
                        .expect("oh youre screwed anyways");

                    encoder
                        .init(cuda_device, size.clone())
                        .expect("encoder init failed");

                    tracing::info!("Encoder initalized for {}x{}", size.width, size.height);
                }

                // Simply shutdown.
                EncoderCommand::Shutdown => break,

                EncoderCommand::ForceKeyframe => {
                    force_keyframe = true;
                }

                EncoderCommand::SendFrame => {
                    // benchmarking
                    use std::time::Instant;
                    let start = Instant::now();

                    // copy gl frame *ON THE GPU* to ffmpeg frame
                    {
                        let gl_ctx = gl_context.lock().expect("couldnt lock egl context");
                        let mut gl_resource =
                            cuda_resource.lock().expect("couldnt lock GL resource!");

                        gl_ctx.make_current();

                        let mut mapped = gl_resource
                            .map()
                            .expect("couldnt map graphics resource. Its joever");

                        let array = mapped
                            .get_mapped_array()
                            .expect("well its all over anyways");

                        let frame = encoder.frame();

                        // setup the cuMemcpy2D operation to copy to the temporary buffer
                        // (we should probably abstract source and provide a way to elide this,
                        // and instead feed ffmpeg directly. for now it's *just* used with gl so /shrug)
                        {
                            memcpy.srcArray = array;

                            unsafe {
                                let frame_ptr = frame.as_mut_ptr();
                                memcpy.dstDevice = *temp_buffer.device_ptr();
                                memcpy.dstPitch = (*frame_ptr).linesize[0] as usize;
                                memcpy.WidthInBytes = ((*frame_ptr).width * 4) as usize;
                                memcpy.Height = (*frame_ptr).height as usize;
                            }
                        }

                        // copy to the temporary buffer and synchronize
                        unsafe {
                            cudarc::driver::sys::lib()
                                .cuMemcpy2DAsync_v2(&memcpy, std::ptr::null_mut())
                                .result()
                                .expect("cuMemcpy2D fail epic");

                            cudarc::driver::sys::lib()
                                .cuStreamSynchronize(std::ptr::null_mut())
                                .result()?;
                        }

                        // launch kernel to flip the opengl framebuffer right-side up
                        {
                            let width = frame.width();
                            let height = frame.height();

                            let launch_config = cudarc::driver::LaunchConfig {
                                grid_dim: (width / 16 + 1, height / 2 + 1, 1),
                                block_dim: (16, 2, 1),
                                shared_mem_bytes: 0,
                            };

                            let flip_opengl = cuda_device.get_func("module", "flip_opengl").expect(
                                "for some reason we couldn't get the support kenrel function",
                            );

                            unsafe {
                                let frame_ptr = frame.as_mut_ptr();

                                let mut slice = cuda_device.upgrade_device_ptr::<u32>(
                                    (*frame_ptr).data[0] as CUdeviceptr,
                                    (width * height) as usize * 4usize,
                                );

                                flip_opengl.launch(
                                    launch_config,
                                    (&mut temp_buffer, &mut slice, width, height),
                                )?;

                                // "leak" so cudarc doesn't try to free the memory
                                // (the device pointer we convert into a slice is owned by ffmpeg, so we shouldn't be the ones
                                //  trying to free it! it will do so later)
                                let _ = slice.leak();

                                // Synchronize for the final time
                                cudarc::driver::sys::lib()
                                    .cuStreamSynchronize(std::ptr::null_mut())
                                    .result()?;
                            }
                        }

                        // FIXME: ideally this would work on-drop but it doesn't.
                        mapped.unmap().expect("fuck you asshole");
                        gl_ctx.release();
                    }

                    frame_number += 1;

                    if let Some(mut pkt) = encoder.send_frame(frame_number as u64, force_keyframe) {
                        // A bit less clear than ::empty(), but it's "Safe"
                        if let Some(_) = pkt.data() {
                            {
                                // Swap the packet and notify a waiting thread that we produced a packet
                                let mut locked_packet =
                                    packet.lock().expect("failed to lock packet");
                                std::mem::swap(&mut *locked_packet, &mut pkt);
                                packet_update.notify_one();
                            }
                        }
                    }

                    if force_keyframe {
                        force_keyframe = false;
                    }

                    if frame_number % 64 == 0 {
                        tracing::info!("encoding frame {frame_number} took {:2?}", start.elapsed());
                    }
                }
            }

            // processed message
            {
                //println!("gggggg?");
                {
                    let mut process_lock = processed.lock().expect("gggg");
                    *process_lock = true;
                    processed_notify.notify_one();
                }
            }
        }
    }

    {
        let mut process_lock = processed.lock().expect("gggg");
        *process_lock = true;
        processed_notify.notify_one();
    }

    Ok(())
}

// TODO: Write notes for using this; some things I can remember:
// - The cuda resource and gl context are shared, therefore
// 	 you have to make sure they are locked and unmapped from whatever
//	 other thread is using them *BEFORE* asking the encoder thread to do something.

/// Creates a thread that will encode a OpenGL framebuffer
/// using ffmpeg hardware-assisted encoding.
pub fn encoder_thread_spawn_hwframe(
    cuda_device: &Arc<CudaDevice>,
    cuda_resource: &Arc<Mutex<GraphicsResource>>,
    gl_context: &Arc<Mutex<DeviceContext>>,

    rgba_to_bgra_kernel: bool,
) -> EncoderThreadControl {
    let processed = Arc::new(Mutex::new(false));
    let processed_cv = Arc::new(Condvar::new());

    let input_updated = Arc::new(Condvar::new());
    let input = Arc::new(Mutex::new(EncoderCommand::ForceKeyframe)); // something dummy

    // packet
    let pkt_update_cv = Arc::new(Condvar::new());
    let pkt = Arc::new(Mutex::new(ffmpeg::Packet::empty()));

    // clones for the thread

    let processed_clone = processed.clone();
    let processed_cv_clone = processed_cv.clone();

    let input_updated_clone = input_updated.clone();
    let input_clone = input.clone();

    let pkt_update_cv_clone = pkt_update_cv.clone();
    let pkt_clone = pkt.clone();

    let dev_clone = Arc::clone(cuda_device);
    let rsrc_clone = Arc::clone(cuda_resource);
    let gl_clone = Arc::clone(gl_context);

    std::thread::spawn(move || {
        match encoder_thread_hwframe_main(
            input_updated_clone,
            input_clone,
            processed_cv_clone,
            processed_clone,
            pkt_update_cv_clone,
            pkt_clone,
            rgba_to_bgra_kernel,
            &dev_clone,
            &rsrc_clone,
            &gl_clone,
        ) {
            Ok(_) => {}
            Err(err) => {
                tracing::error!("encoder thread error: {err}");
            }
        }
    });

    EncoderThreadControl {
        input_updated_cv: input_updated.clone(),
        input: input.clone(),
        processed: processed.clone(),
        processed_cv: processed_cv.clone(),
        packet_updated_cv: pkt_update_cv.clone(),
        packet: pkt.clone(),
    }
}
