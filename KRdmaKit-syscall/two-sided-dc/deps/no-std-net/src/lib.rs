// Effectively all the code in this repo is copied with permission from Rust's std library.
// They hold the copyright (http://rust-lang.org/COPYRIGHT) and whatever other rights, but this
// crate is MIT licensed also, so it's all good.

//! Networking primitives for TCP/UDP communication.
//!
//! This module provides networking functionality for the Transmission Control and User
//! Datagram Protocols, as well as types for IP and socket addresses.  It has been ported
//! from std::net to remove the dependency on std.
//!
//! This crate is a WIP, issues, feedback and PRs are welcome as long as they follow the theme of
//! "std::net" clone.
//!
//! # Organization
//!
//! * [`IpAddr`] represents IP addresses of either IPv4 or IPv6; [`Ipv4Addr`] and
//!   [`Ipv6Addr`] are respectively IPv4 and IPv6 addresses
//! * [`TcpListener`] and [`TcpStream`] provide functionality for communication over TCP
//! * [`UdpSocket`] provides functionality for communication over UDP
//! * [`SocketAddr`] represents socket addresses of either IPv4 or IPv6; [`SocketAddrV4`]
//!   and [`SocketAddrV6`] are respectively IPv4 and IPv6 socket addresses
//! * [`ToSocketAddrs`] is a trait that used for generic address resolution when interacting
//!   with networking objects like [`TcpListener`], [`TcpStream`] or [`UdpSocket`]
//! * Other types are return or parameter types for various methods in this module
//!
//! [`IpAddr`]: ../../no-std-net/enum.IpAddr.html
//! [`Ipv4Addr`]: ../../no-std-net/struct.Ipv4Addr.html
//! [`Ipv6Addr`]: ../../no-std-net/struct.Ipv6Addr.html
//! [`SocketAddr`]: ../../std/net/enum.SocketAddr.html
//! [`SocketAddrV4`]: ../../std/net/struct.SocketAddrV4.html
//! [`SocketAddrV6`]: ../../std/net/struct.SocketAddrV6.html
//! [`TcpListener`]: ../../std/net/struct.TcpListener.html
//! [`TcpStream`]: ../../std/net/struct.TcpStream.html
//! [`ToSocketAddrs`]: ../../std/net/trait.ToSocketAddrs.html
//! [`UdpSocket`]: ../../std/net/struct.UdpSocket.html

// TODO: figure out how to put links into rustdocs and update the above

#![allow(clippy::all)]
#![no_std]
#![deny(
	dead_code,
//	missing_docs,
	unused_imports,
	unused_must_use,
	unused_parens,
	unused_qualifications,
//	warnings,
)]
#![forbid(unsafe_code)]
use core::fmt;

mod addr;
mod ip;
pub mod parser;

#[cfg(feature = "serde")]
extern crate serde;
#[cfg(feature = "serde")]
mod de;
#[cfg(feature = "serde")]
mod ser;

pub use addr::{SocketAddr, SocketAddrV4, SocketAddrV6, ToSocketAddrs};
pub use ip::{IpAddr, Ipv4Addr, Ipv6Addr, Ipv6MulticastScope};

// RDMA GID
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash, PartialOrd, Ord)]
pub struct Guid {
    inner: [u8; 16],
}

impl Guid {
    pub fn new_u8(inner: &[u8; 16]) -> Self {
        Self { inner: *inner }
    }

    pub fn get_raw(&self) -> [u8; 16] {
        self.inner
    }

    #[allow(clippy::many_single_char_names)]
    pub fn new(a: u16, b: u16, c: u16, d: u16, e: u16, f: u16, g: u16, h: u16) -> Self {
        Self {
            inner: [
                (a >> 8) as u8,
                a as u8,
                (b >> 8) as u8,
                b as u8,
                (c >> 8) as u8,
                c as u8,
                (d >> 8) as u8,
                d as u8,
                (e >> 8) as u8,
                e as u8,
                (f >> 8) as u8,
                f as u8,
                (g >> 8) as u8,
                g as u8,
                (h >> 8) as u8,
                h as u8,
            ],
        }
    }
    pub fn segments(&self) -> [u16; 8] {
        let arr = &self.inner;
        [
            (arr[0] as u16) << 8 | (arr[1] as u16),
            (arr[2] as u16) << 8 | (arr[3] as u16),
            (arr[4] as u16) << 8 | (arr[5] as u16),
            (arr[6] as u16) << 8 | (arr[7] as u16),
            (arr[8] as u16) << 8 | (arr[9] as u16),
            (arr[10] as u16) << 8 | (arr[11] as u16),
            (arr[12] as u16) << 8 | (arr[13] as u16),
            (arr[14] as u16) << 8 | (arr[15] as u16),
        ]
    }
}

impl core::fmt::Display for Guid {
    fn fmt(&self, fmt: &mut ::core::fmt::Formatter) -> core::fmt::Result {
        let seg = self.segments();
        write!(
            fmt,
            "{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}",
            seg[0], seg[1], seg[2], seg[3], seg[4], seg[5], seg[6], seg[7]
        )
    }
}
