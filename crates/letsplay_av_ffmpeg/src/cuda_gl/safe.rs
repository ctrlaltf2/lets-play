use cudarc::driver::{result as cuda_result, sys as cuda_sys, CudaDevice};

use std::sync::Arc;

pub struct MappedGraphicsResource {
    resource: cuda_sys::CUgraphicsResource,
}

impl MappedGraphicsResource {
    fn new(resource: cuda_sys::CUgraphicsResource) -> Self {
        Self { resource }
    }

    pub fn map(&mut self) -> Result<(), cuda_result::DriverError> {
        unsafe {
            cuda_sys::lib()
                .cuGraphicsMapResources(1, &mut self.resource, std::ptr::null_mut())
                .result()?;
        }
        Ok(())
    }

    pub fn unmap(&mut self) -> Result<(), cuda_result::DriverError> {
        unsafe {
            cuda_sys::lib()
                .cuGraphicsUnmapResources(1, &mut self.resource, std::ptr::null_mut())
                .result()?;
        }

        Ok(())
    }

    pub fn get_mapped_array(&mut self) -> Result<cuda_sys::CUarray, cuda_result::DriverError> {
        let mut array: cuda_sys::CUarray = std::ptr::null_mut();

        unsafe {
            cuda_sys::lib()
                .cuGraphicsSubResourceGetMappedArray(&mut array, self.resource, 0, 0)
                .result()?;
        }

        Ok(array)
    }

    pub fn get_device_pointer(
        &mut self,
    ) -> Result<cuda_sys::CUdeviceptr, cuda_result::DriverError> {
        let mut array: cuda_sys::CUdeviceptr = 0;
        let mut size: usize = 0;

        unsafe {
            cuda_sys::lib()
                .cuGraphicsResourceGetMappedPointer_v2(&mut array, &mut size, self.resource)
                .result()?;
        }

        Ok(array)
    }
}

impl Drop for MappedGraphicsResource {
    fn drop(&mut self) {
        let _ = self.unmap();
    }
}

/// Wrapper over cuGraphicsGL* apis
pub struct GraphicsResource {
    context: Arc<CudaDevice>,
    resource: cuda_sys::CUgraphicsResource,
}

impl GraphicsResource {
    pub fn new(device: &Arc<CudaDevice>) -> Self {
        Self {
            context: device.clone(),
            resource: std::ptr::null_mut(),
        }
    }

    pub fn device(&self) -> Arc<CudaDevice> {
        self.context.clone()
    }

    /// Maps this resource.
    pub fn map(&mut self) -> Result<MappedGraphicsResource, cuda_result::DriverError> {
        assert!(self.is_registered(), "Must register a resource before calling GraphicsResource::map()");

        let mut res = MappedGraphicsResource::new(self.resource);
        res.map()?;

        Ok(res)
    }

    pub fn register(
        &mut self,
        texture_id: gl::types::GLuint,
        texture_kind: gl::types::GLuint,
    ) -> Result<(), cuda_result::DriverError> {
        // better to be safe than leak memory? idk.
        if self.is_registered() {
            self.unregister()?;
        }

        unsafe {
            super::lib()
                .cuGraphicsGLRegisterImage(&mut self.resource, texture_id, texture_kind, 1)
                .result()?;
        }

        Ok(())
    }

    pub fn is_registered(&self) -> bool {
        !self.resource.is_null()
    }

    pub fn unregister(&mut self) -> Result<(), cuda_result::DriverError> {
        // Don't need to unregister if nothing's actually registered
        if !self.is_registered() {
            return Ok(());
        }

        unsafe {
            cuda_sys::lib()
                .cuGraphicsUnregisterResource(self.resource)
                .result()?;
        }

        self.resource = std::ptr::null_mut();
        Ok(())
    }
}

impl Drop for GraphicsResource {
    fn drop(&mut self) {
        if self.is_registered() {
            let _ = self.unregister();
        }
    }
}

unsafe impl Send for GraphicsResource {}
