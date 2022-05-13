mod client;
mod sidr;

pub use client::ClientCM;
pub use sidr::{EndPoint, SidrCM};

pub type ServerCM = ClientCM;