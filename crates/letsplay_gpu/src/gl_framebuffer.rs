use std::ffi;
use std::ptr::{addr_of_mut, null};

/// helper wrapper over a OpenGL Frame Buffer Object (FBO).
pub struct GlFramebuffer {
	// OpenGL object IDs
	texture_id: gl::types::GLuint,
	renderbuffer_id: gl::types::GLuint,
	fbo_id: gl::types::GLuint,
}

pub struct BindGuard {}

impl BindGuard {
	fn new(fbo_id: gl::types::GLuint) -> Self {
		unsafe {
			gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_id);
		}
		Self {}
	}
}

impl Drop for BindGuard {
	fn drop(&mut self) {
		unsafe {
			gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
		}
	}
}

impl GlFramebuffer {
	pub fn new() -> Self {
		Self {
			fbo_id: 0,
			texture_id: 0,
			renderbuffer_id: 0,
		}
	}

	/// Destroys this framebuffer.
	///
	/// All OpenGL FBO resources (the FBO itself, the render texture, and the renderbuffer used for depth) are deleted by this call.
	pub fn destroy(&mut self) {
		unsafe {
			gl::DeleteFramebuffers(1, addr_of_mut!(self.fbo_id));
			self.fbo_id = 0;

			gl::DeleteTextures(1, addr_of_mut!(self.texture_id));
			self.texture_id = 0;

			gl::DeleteRenderbuffers(1, addr_of_mut!(self.renderbuffer_id));
			self.renderbuffer_id = 0;
		}
	}

	/// Creates the OpenGL FBO.
	pub fn resize(&mut self, width: u32, height: u32) {
		unsafe {
			if self.fbo_id != 0 {
				self.destroy();
			}

			gl::GenTextures(1, addr_of_mut!(self.texture_id));
			gl::BindTexture(gl::TEXTURE_2D, self.texture_id);

			gl::TexImage2D(
				gl::TEXTURE_2D,
				0,
				gl::RGBA8 as i32,
				width as i32,
				height as i32,
				0,
				gl::RGBA,
				gl::UNSIGNED_BYTE,
				null(),
			);

			gl::BindTexture(gl::TEXTURE_2D, 0);

			gl::GenRenderbuffers(1, addr_of_mut!(self.renderbuffer_id));
			gl::BindRenderbuffer(gl::RENDERBUFFER, self.renderbuffer_id);

			gl::RenderbufferStorage(
				gl::RENDERBUFFER,
				gl::DEPTH_COMPONENT,
				width as i32,
				height as i32,
			);

			gl::BindRenderbuffer(gl::RENDERBUFFER, 0);

			gl::GenFramebuffers(1, addr_of_mut!(self.fbo_id));
			gl::BindFramebuffer(gl::FRAMEBUFFER, self.fbo_id);

			gl::FramebufferTexture2D(
				gl::FRAMEBUFFER,
				gl::COLOR_ATTACHMENT0,
				gl::TEXTURE_2D,
				self.texture_id,
				0,
			);

			gl::FramebufferRenderbuffer(
				gl::FRAMEBUFFER,
				gl::DEPTH_ATTACHMENT,
				gl::RENDERBUFFER,
				self.renderbuffer_id,
			);

			gl::Viewport(0, 0, width as i32, height as i32);

			gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
		}
	}

	pub fn as_raw(&self) -> gl::types::GLuint {
		self.fbo_id
	}

	// TODO: accessors for the render texture

	/// Binds this framebuffer in the current scope.
	pub fn bind(&self) -> BindGuard {
		BindGuard::new(self.fbo_id)
	}

	/// Reads pixels to a CPU-side buffer. Uses glReadPixels so it probably sucks.
	pub fn read_pixels(&self, buffer: &mut [u32], width: u32, height: u32) {
		let _guard = self.bind();

		assert_eq!(
			buffer.len(),
			(width * height) as usize,
			"Provided buffer cannot hold the framebuffer"
		);

		// SAFETY: The above assertion prevents the following code from
		// violating memory safety by appropiately asserting the invariant
		// that we must have width * heigth pixels of space to write to.
		unsafe {
			gl::ReadPixels(
				0,
				0,
				width as i32,
				height as i32,
				gl::RGBA,
				gl::UNSIGNED_BYTE,
				buffer.as_mut_ptr() as *mut ffi::c_void,
			);
		}
	}
}
