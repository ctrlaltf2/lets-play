//! GPU context information structs

#[cfg(feature = "av-nvidia")]
use cudarc::driver::CudaDevice;

use letsplay_gpu::egl_helpers::DeviceContext;

use std::sync::{Arc, Mutex};

#[cfg(feature = "av-nvidia")]
pub struct GraphicsContexts {
	pub egl_device_context: Arc<Mutex<DeviceContext>>,
	pub cuda_context: Arc<CudaDevice>,
}

#[cfg(not(feature = "av-nvidia"))]
pub struct GraphicsContexts {
	pub egl_device_context: Arc<Mutex<DeviceContext>>,
}

impl GraphicsContexts {
	/// Creates all applicable contexts for a given implementation.
	pub fn create(gpu_index: usize) -> Self {
		let contexts = {
			#[cfg(feature = "av-nvidia")]
			{
				GraphicsContexts {
					cuda_context: CudaDevice::new(gpu_index).expect("???"),
					egl_device_context: Arc::new(Mutex::new(DeviceContext::new(gpu_index))),
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
