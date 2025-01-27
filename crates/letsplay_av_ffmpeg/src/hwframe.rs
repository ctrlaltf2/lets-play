use std::ptr::null_mut;

use super::ffmpeg;

use ffmpeg::format::Pixel;

use super::{check_ret, hwdevice::DeviceContext};

/// A context that can allocate hardware frames.
pub struct HwFrameContext {
    _cuda_device_context: DeviceContext,
    buffer: *mut ffmpeg::sys::AVBufferRef,
}

impl HwFrameContext {
    fn new(device_context: DeviceContext, buffer: *mut ffmpeg::sys::AVBufferRef) -> Self {
        Self {
            _cuda_device_context: device_context,
            buffer,
        }
    }

    // pub fn as_context_mut(&mut self) -> &mut ffmpeg::sys::AVHWFramesContext {
    // 	unsafe { &mut *((*self.buffer).data as *mut ffmpeg::sys::AVHWFramesContext) }
    // }

    // pub fn as_context(&self) -> &ffmpeg::sys::AVHWFramesContext {
    // 	unsafe { &*((*self.buffer).data as *const ffmpeg::sys::AVHWFramesContext) }
    // }

    pub fn as_raw_mut(&mut self) -> &mut ffmpeg::sys::AVBufferRef {
        unsafe { &mut *self.buffer }
    }

    pub fn as_device_context_mut(&mut self) -> &mut ffmpeg::sys::AVBufferRef {
        self._cuda_device_context.as_raw_mut()
    }

    /// call once to allocate frame
    pub fn get_buffer(&mut self, frame: &mut ffmpeg::frame::Video) -> Result<(), ffmpeg::Error> {
        unsafe {
            super::check_ret(ffmpeg::sys::av_hwframe_get_buffer(
                self.buffer,
                frame.as_mut_ptr(),
                0,
            ))?;
        }

        Ok(())
    }

    // pub fn as_raw(&self) -> &ffmpeg::sys::AVBufferRef {
    // 	unsafe { &*self.buffer }
    // }
}

unsafe impl Send for HwFrameContext {}

impl Drop for HwFrameContext {
    fn drop(&mut self) {
        unsafe {
            if !self.buffer.is_null() {
                ffmpeg::sys::av_buffer_unref(&mut self.buffer);
            }
        }
    }
}

/// A builder for [HwFrameContext]s.
pub struct HwFrameContextBuilder {
    device_context: DeviceContext,
    buffer: *mut ffmpeg::sys::AVBufferRef,
}

impl HwFrameContextBuilder {
    pub fn new(mut device_context: DeviceContext) -> anyhow::Result<Self> {
        let buffer = unsafe { ffmpeg::sys::av_hwframe_ctx_alloc(device_context.as_raw_mut()) };
        if buffer.is_null() {
            return Err(anyhow::anyhow!("Could not allocate a hwframe context"));
        }

        Ok(Self {
            device_context,
            buffer,
        })
    }

    pub fn build(mut self) -> Result<HwFrameContext, ffmpeg::Error> {
        check_ret(unsafe { ffmpeg::sys::av_hwframe_ctx_init(self.buffer) })?;
        let result = Ok(HwFrameContext::new(self.device_context, self.buffer));
        self.buffer = null_mut();

        result
    }

    pub fn set_width(mut self, width: u32) -> Self {
        self.as_frame_mut().width = width as i32;
        self
    }

    pub fn set_height(mut self, height: u32) -> Self {
        self.as_frame_mut().height = height as i32;
        self
    }

    pub fn set_sw_format(mut self, sw_format: Pixel) -> Self {
        self.as_frame_mut().sw_format = sw_format.into();
        self
    }

    pub fn set_format(mut self, format: Pixel) -> Self {
        self.as_frame_mut().format = format.into();
        self
    }

    pub fn as_frame_mut(&mut self) -> &mut ffmpeg::sys::AVHWFramesContext {
        unsafe { &mut *((*self.buffer).data as *mut ffmpeg::sys::AVHWFramesContext) }
    }

    // pub fn as_frame(&self) -> &ffmpeg::sys::AVHWFramesContext {
    // 	unsafe { &*((*self.buffer).data as *const ffmpeg::sys::AVHWFramesContext) }
    // }
}
