//! Shared types.

#[derive(Clone, Debug)]
pub struct Rect {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}

//#[derive(Debug)]
//pub struct Point {
//    pub x: u32,
//    pub y: u32,
//}

#[derive(Clone, Debug)]
pub struct Size {
    pub width: u32,
    pub height: u32,
}

impl Size {
    /// Returns the linear size.
    pub fn linear(&self) -> usize {
        (self.width * self.height) as usize
    }
}

impl From<(u32, u32)> for Size {
    fn from(value: (u32, u32)) -> Self {
        Size {
            width: value.0,
            height: value.1,
        }
    }
}

impl Into<(u32, u32)> for Size {
    fn into(self) -> (u32, u32) {
        (self.width, self.height)
    }
}
