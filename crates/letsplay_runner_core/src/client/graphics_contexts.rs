//! GPU context information structs

#[cfg(feature = "av-nvidia")]
use cudarc::driver::CudaDevice;

#[cfg(feature = "av-nvidia")]
use letsplay_av_ffmpeg::cuda_gl::safe::GraphicsResource;

use letsplay_gpu::egl_helpers::DeviceContext;

use std::sync::{Arc, Mutex};

#[cfg(feature = "av-nvidia")]
#[derive(Clone)]
pub struct GraphicsContexts {
	pub egl_device_context: Arc<Mutex<DeviceContext>>,
	pub cuda_context: Arc<CudaDevice>,
	pub cuda_interop_context: Arc<Mutex<GraphicsResource>>,
}

#[cfg(not(feature = "av-nvidia"))]
#[derive(Clone)]
pub struct GraphicsContexts {
	pub egl_device_context: Arc<Mutex<DeviceContext>>,
}

impl GraphicsContexts {
	/// Creates all applicable contexts for a given implementation.
	pub fn create(gpu_index: usize) -> Self {
		let contexts = {
			#[cfg(feature = "av-nvidia")]
			{
				let cuda_context = CudaDevice::new(gpu_index).expect("???");
				GraphicsContexts {
					cuda_context: cuda_context.clone(),
					egl_device_context: Arc::new(Mutex::new(DeviceContext::new(gpu_index))),
					cuda_interop_context: Arc::new(Mutex::new(GraphicsResource::new(
						&cuda_context.clone(),
					))),
				}
			}

			#[cfg(not(feature = "av-nvidia"))]
			{
				GraphicsContexts {
					egl_device_context: Arc::new(Mutex::new(DeviceContext::new(gpu_index))),
				}
			}
		};

		contexts
	}
}
