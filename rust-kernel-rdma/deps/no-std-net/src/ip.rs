// Effectively all the code in this repo is copied with permission from Rust's std library.
// They hold the copyright (http://rust-lang.org/COPYRIGHT) and whatever other rights, but this
// crate is MIT licensed also, so it's all good.

use core::cmp::Ordering;

// TODO: copy the parsers over from https://github.com/rust-lang/rust/blob/master/src/libstd/net/parser.rs
// and update all the tests

/// An IP address, either IPv4 or IPv6.
///
/// This enum can contain either an [`Ipv4Addr`] or an [`Ipv6Addr`], see their
/// respective documentation for more details.
///
/// [`Ipv4Addr`]: ../../no-std-net/struct.Ipv4Addr.html
/// [`Ipv6Addr`]: ../../no-std-net/struct.Ipv6Addr.html
///
/// # Examples
///
/// ```
/// use no_std_net::{IpAddr, Ipv4Addr, Ipv6Addr};
///
/// let localhost_v4 = IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1));
/// let localhost_v6 = IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1));
///
/// assert_eq!("127.0.0.1".parse(), Ok(localhost_v4));
/// assert_eq!("::1".parse(), Ok(localhost_v6));
///
/// assert_eq!(localhost_v4.is_ipv6(), false);
/// assert_eq!(localhost_v4.is_ipv4(), true);
/// ```
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash, PartialOrd, Ord)]
pub enum IpAddr {
    /// An IPv4 address.
    V4(Ipv4Addr),
    /// An IPv6 address.
    V6(Ipv6Addr),
}

/// An IPv4 address.
///
/// IPv4 addresses are defined as 32-bit integers in [IETF RFC 791].
/// They are usually represented as four octets.
///
/// See [`IpAddr`] for a type encompassing both IPv4 and IPv6 addresses.
///
/// [IETF RFC 791]: https://tools.ietf.org/html/rfc791
/// [`IpAddr`]: ../../no-std-net/enum.IpAddr.html
///
/// # Textual representation
///
/// `Ipv4Addr` provides a [`FromStr`] implementation. The four octets are in decimal
/// notation, divided by `.` (this is called "dot-decimal notation").
///
/// [`FromStr`]: https://doc.rust-lang.org/core/str/trait.FromStr.html
///
/// # Examples
///
/// ```
/// use no_std_net::Ipv4Addr;
///
/// let localhost = Ipv4Addr::new(127, 0, 0, 1);
/// assert_eq!("127.0.0.1".parse(), Ok(localhost));
/// assert_eq!(localhost.is_loopback(), true);
/// ```
#[derive(Clone, Copy, Eq, PartialEq, Hash, PartialOrd, Ord)]
pub struct Ipv4Addr {
    // Octets stored in transmit order.
    inner: [u8; 4],
}

impl Ipv4Addr {
    pub fn get_inner_as_sockaddr(&self) -> [i8; 14] {
        [
            self.inner[0] as i8,
            self.inner[1] as i8,
            self.inner[2] as i8,
            self.inner[3] as i8,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
        ]
    }
}

// TODO: clean up all the links in the documentation!

/// An IPv6 address.
///
/// IPv6 addresses are defined as 128-bit integers in [IETF RFC 4291].
/// They are usually represented as eight 16-bit segments.
///
/// See [`IpAddr`] for a type encompassing both IPv4 and IPv6 addresses.
///
/// [IETF RFC 4291]: https://tools.ietf.org/html/rfc4291
/// [`IpAddr`]: ../../no-std-net/enum.IpAddr.html
///
/// # Textual representation
///
/// `Ipv6Addr` provides a [`FromStr`] implementation. There are many ways to represent
/// an IPv6 address in text, but in general, each segments is written in hexadecimal
/// notation, and segments are separated by `:`. For more information, see
/// [IETF RFC 5952].
///
/// [`FromStr`]: https://doc.rust-lang.org/core/str/trait.FromStr.html
/// [IETF RFC 5952]: https://tools.ietf.org/html/rfc5952
///
/// # Examples
///
/// ```
/// use no_std_net::Ipv6Addr;
///
/// let localhost = Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1);
/// assert_eq!("::1".parse(), Ok(localhost));
/// assert_eq!(localhost.is_loopback(), true);
/// ```
#[derive(Clone, Copy, Eq, PartialEq, Hash, PartialOrd, Ord)]
pub struct Ipv6Addr {
    // Octets stored in transmit order.
    inner: [u8; 16],
}

#[allow(missing_docs)]
#[derive(Copy, PartialEq, Eq, Clone, Hash, Debug)]
pub enum Ipv6MulticastScope {
    InterfaceLocal,
    LinkLocal,
    RealmLocal,
    AdminLocal,
    SiteLocal,
    OrganizationLocal,
    Global,
}

impl IpAddr {
    /// Returns [`true`] for the special 'unspecified' address.
    ///
    /// See the documentation for [`Ipv4Addr::is_unspecified`][IPv4] and
    /// [`Ipv6Addr::is_unspecified`][IPv6] for more details.
    ///
    /// [IPv4]: ../../no-std-net/struct.Ipv4Addr.html#method.is_unspecified
    /// [IPv6]: ../../no-std-net/struct.Ipv6Addr.html#method.is_unspecified
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv4Addr, Ipv6Addr};
    ///
    /// assert_eq!(IpAddr::V4(Ipv4Addr::new(0, 0, 0, 0)).is_unspecified(), true);
    /// assert_eq!(IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0)).is_unspecified(), true);
    /// ```
    pub fn is_unspecified(&self) -> bool {
        match *self {
            IpAddr::V4(ref a) => a.is_unspecified(),
            IpAddr::V6(ref a) => a.is_unspecified(),
        }
    }

    /// Returns [`true`] if this is a loopback address.
    ///
    /// See the documentation for [`Ipv4Addr::is_loopback`][IPv4] and
    /// [`Ipv6Addr::is_loopback`][IPv6] for more details.
    ///
    /// [IPv4]: ../../no-std-net/struct.Ipv4Addr.html#method.is_loopback
    /// [IPv6]: ../../no-std-net/struct.Ipv6Addr.html#method.is_loopback
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv4Addr, Ipv6Addr};
    ///
    /// assert_eq!(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)).is_loopback(), true);
    /// assert_eq!(IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0x1)).is_loopback(), true);
    /// ```
    pub fn is_loopback(&self) -> bool {
        match *self {
            IpAddr::V4(ref a) => a.is_loopback(),
            IpAddr::V6(ref a) => a.is_loopback(),
        }
    }

    /// Returns [`true`] if the address appears to be globally routable.
    ///
    /// See the documentation for [`Ipv4Addr::is_global`][IPv4] and
    /// [`Ipv6Addr::is_global`][IPv6] for more details.
    ///
    /// [IPv4]: ../../no-std-net/struct.Ipv4Addr.html#method.is_global
    /// [IPv6]: ../../no-std-net/struct.Ipv6Addr.html#method.is_global
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv4Addr, Ipv6Addr};
    ///
    /// fn main() {
    ///     assert_eq!(IpAddr::V4(Ipv4Addr::new(80, 9, 12, 3)).is_global(), true);
    ///     assert_eq!(IpAddr::V6(Ipv6Addr::new(0, 0, 0x1c9, 0, 0, 0xafc8, 0, 0x1)).is_global(),
    ///                true);
    /// }
    /// ```
    pub fn is_global(&self) -> bool {
        match *self {
            IpAddr::V4(ref a) => a.is_global(),
            IpAddr::V6(ref a) => a.is_global(),
        }
    }

    /// Returns [`true`] if this is a multicast address.
    ///
    /// See the documentation for [`Ipv4Addr::is_multicast`][IPv4] and
    /// [`Ipv6Addr::is_multicast`][IPv6] for more details.
    ///
    /// [IPv4]: ../../no-std-net/struct.Ipv4Addr.html#method.is_multicast
    /// [IPv6]: ../../no-std-net/struct.Ipv6Addr.html#method.is_multicast
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv4Addr, Ipv6Addr};
    ///
    /// assert_eq!(IpAddr::V4(Ipv4Addr::new(224, 254, 0, 0)).is_multicast(), true);
    /// assert_eq!(IpAddr::V6(Ipv6Addr::new(0xff00, 0, 0, 0, 0, 0, 0, 0)).is_multicast(), true);
    /// ```
    pub fn is_multicast(&self) -> bool {
        match *self {
            IpAddr::V4(ref a) => a.is_multicast(),
            IpAddr::V6(ref a) => a.is_multicast(),
        }
    }

    /// Returns [`true`] if this address is in a range designated for documentation.
    ///
    /// See the documentation for [`Ipv4Addr::is_documentation`][IPv4] and
    /// [`Ipv6Addr::is_documentation`][IPv6] for more details.
    ///
    /// [IPv4]: ../../no-std-net/struct.Ipv4Addr.html#method.is_documentation
    /// [IPv6]: ../../no-std-net/struct.Ipv6Addr.html#method.is_documentation
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv4Addr, Ipv6Addr};
    ///
    /// fn main() {
    ///     assert_eq!(IpAddr::V4(Ipv4Addr::new(203, 0, 113, 6)).is_documentation(), true);
    ///     assert_eq!(IpAddr::V6(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0))
    ///                       .is_documentation(), true);
    /// }
    /// ```
    pub fn is_documentation(&self) -> bool {
        match *self {
            IpAddr::V4(ref a) => a.is_documentation(),
            IpAddr::V6(ref a) => a.is_documentation(),
        }
    }

    /// Returns [`true`] if this address is an [IPv4 address], and [`false`] otherwise.
    ///
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    /// [`false`]: https://doc.rust-lang.org/std/primitive.bool.html
    /// [IPv4 address]: #variant.V4
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv4Addr, Ipv6Addr};
    ///
    /// fn main() {
    ///     assert_eq!(IpAddr::V4(Ipv4Addr::new(203, 0, 113, 6)).is_ipv4(), true);
    ///     assert_eq!(IpAddr::V6(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0)).is_ipv4(),
    ///                false);
    /// }
    /// ```
    pub fn is_ipv4(&self) -> bool {
        match *self {
            IpAddr::V4(_) => true,
            IpAddr::V6(_) => false,
        }
    }

    /// Returns [`true`] if this address is an [IPv6 address], and [`false`] otherwise.
    ///
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    /// [`false`]: https://doc.rust-lang.org/std/primitive.bool.html
    /// [IPv6 address]: #variant.V6
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv4Addr, Ipv6Addr};
    ///
    /// fn main() {
    ///     assert_eq!(IpAddr::V4(Ipv4Addr::new(203, 0, 113, 6)).is_ipv6(), false);
    ///     assert_eq!(IpAddr::V6(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0)).is_ipv6(),
    ///                true);
    /// }
    /// ```
    pub fn is_ipv6(&self) -> bool {
        match *self {
            IpAddr::V4(_) => false,
            IpAddr::V6(_) => true,
        }
    }
}

impl ::fmt::Display for IpAddr {
    fn fmt(&self, fmt: &mut ::fmt::Formatter) -> ::fmt::Result {
        match *self {
            IpAddr::V4(ref a) => a.fmt(fmt),
            IpAddr::V6(ref a) => a.fmt(fmt),
        }
    }
}

impl From<[u8; 4]> for Ipv4Addr {
    /// Creates an `Ipv4Addr` from a four element byte array.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// let addr = Ipv4Addr::from([13u8, 12u8, 11u8, 10u8]);
    /// assert_eq!(Ipv4Addr::new(13, 12, 11, 10), addr);
    /// ```
    fn from(octets: [u8; 4]) -> Ipv4Addr {
        Ipv4Addr::new(octets[0], octets[1], octets[2], octets[3])
    }
}

impl From<[u8; 4]> for IpAddr {
    /// Creates an `IpAddr::V4` from a four element byte array.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv4Addr};
    ///
    /// let addr = IpAddr::from([13u8, 12u8, 11u8, 10u8]);
    /// assert_eq!(IpAddr::V4(Ipv4Addr::new(13, 12, 11, 10)), addr);
    /// ```
    fn from(octets: [u8; 4]) -> IpAddr {
        IpAddr::V4(Ipv4Addr::from(octets))
    }
}

impl From<[u8; 16]> for Ipv6Addr {
    /// Creates an `Ipv6Addr` from a sixteen element byte array.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// let addr = Ipv6Addr::from([
    ///     25u8, 24u8, 23u8, 22u8, 21u8, 20u8, 19u8, 18u8,
    ///     17u8, 16u8, 15u8, 14u8, 13u8, 12u8, 11u8, 10u8,
    /// ]);
    /// assert_eq!(
    ///     Ipv6Addr::new(
    ///         0x1918, 0x1716,
    ///         0x1514, 0x1312,
    ///         0x1110, 0x0f0e,
    ///         0x0d0c, 0x0b0a
    ///     ),
    ///     addr
    /// );
    /// ```
    fn from(octets: [u8; 16]) -> Ipv6Addr {
        Ipv6Addr { inner: octets }
    }
}

impl From<[u16; 8]> for Ipv6Addr {
    /// Creates an `Ipv6Addr` from an eight element 16-bit array.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// let addr = Ipv6Addr::from([
    ///     525u16, 524u16, 523u16, 522u16,
    ///     521u16, 520u16, 519u16, 518u16,
    /// ]);
    /// assert_eq!(
    ///     Ipv6Addr::new(
    ///         0x20d, 0x20c,
    ///         0x20b, 0x20a,
    ///         0x209, 0x208,
    ///         0x207, 0x206
    ///     ),
    ///     addr
    /// );
    /// ```
    #[allow(clippy::many_single_char_names)]
    fn from(segments: [u16; 8]) -> Ipv6Addr {
        let [a, b, c, d, e, f, g, h] = segments;
        Ipv6Addr::new(a, b, c, d, e, f, g, h)
    }
}

impl From<[u8; 16]> for IpAddr {
    /// Creates an `IpAddr::V6` from a sixteen element byte array.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv6Addr};
    ///
    /// let addr = IpAddr::from([
    ///     25u8, 24u8, 23u8, 22u8, 21u8, 20u8, 19u8, 18u8,
    ///     17u8, 16u8, 15u8, 14u8, 13u8, 12u8, 11u8, 10u8,
    /// ]);
    /// assert_eq!(
    ///     IpAddr::V6(Ipv6Addr::new(
    ///         0x1918, 0x1716,
    ///         0x1514, 0x1312,
    ///         0x1110, 0x0f0e,
    ///         0x0d0c, 0x0b0a
    ///     )),
    ///     addr
    /// );
    /// ```
    fn from(octets: [u8; 16]) -> IpAddr {
        IpAddr::V6(Ipv6Addr::from(octets))
    }
}

impl From<[u16; 8]> for IpAddr {
    /// Creates an `IpAddr::V6` from an eight element 16-bit array.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv6Addr};
    ///
    /// let addr = IpAddr::from([
    ///     525u16, 524u16, 523u16, 522u16,
    ///     521u16, 520u16, 519u16, 518u16,
    /// ]);
    /// assert_eq!(
    ///     IpAddr::V6(Ipv6Addr::new(
    ///         0x20d, 0x20c,
    ///         0x20b, 0x20a,
    ///         0x209, 0x208,
    ///         0x207, 0x206
    ///     )),
    ///     addr
    /// );
    /// ```
    fn from(segments: [u16; 8]) -> IpAddr {
        IpAddr::V6(Ipv6Addr::from(segments))
    }
}

impl From<Ipv4Addr> for IpAddr {
    /// Copies this address to a new `IpAddr::V4`.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv4Addr};
    ///
    /// let addr = Ipv4Addr::new(127, 0, 0, 1);
    ///
    /// assert_eq!(
    ///     IpAddr::V4(addr),
    ///     IpAddr::from(addr)
    /// )
    /// ```
    fn from(ipv4: Ipv4Addr) -> IpAddr {
        IpAddr::V4(ipv4)
    }
}

impl From<Ipv6Addr> for IpAddr {
    /// Copies this address to a new `IpAddr::V6`.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{IpAddr, Ipv6Addr};
    ///
    /// let addr = Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff);
    ///
    /// assert_eq!(
    ///     IpAddr::V6(addr),
    ///     IpAddr::from(addr)
    /// );
    /// ```
    fn from(ipv6: Ipv6Addr) -> IpAddr {
        IpAddr::V6(ipv6)
    }
}

impl Ipv4Addr {
    /// Creates a new IPv4 address from four eight-bit octets.
    ///
    /// The result will represent the IP address `a`.`b`.`c`.`d`.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// let addr = Ipv4Addr::new(127, 0, 0, 1);
    /// ```
    pub fn new(a: u8, b: u8, c: u8, d: u8) -> Ipv4Addr {
        Ipv4Addr {
            inner: [a, b, c, d],
        }
    }

    /// Creates a new IPv4 address with the address pointing to localhost: 127.0.0.1.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// let addr = Ipv4Addr::localhost();
    /// assert_eq!(addr, Ipv4Addr::new(127, 0, 0, 1));
    /// ```
    pub fn localhost() -> Ipv4Addr {
        Ipv4Addr::new(127, 0, 0, 1)
    }

    /// Creates a new IPv4 address representing an unspecified address: 0.0.0.0
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// let addr = Ipv4Addr::unspecified();
    /// assert_eq!(addr, Ipv4Addr::new(0, 0, 0, 0));
    /// ```
    pub fn unspecified() -> Ipv4Addr {
        Ipv4Addr::new(0, 0, 0, 0)
    }

    /// Returns the four eight-bit integers that make up this address.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// let addr = Ipv4Addr::new(127, 0, 0, 1);
    /// assert_eq!(addr.octets(), [127, 0, 0, 1]);
    /// ```
    pub fn octets(&self) -> [u8; 4] {
        self.inner.clone()
    }

    /// Returns [`true`] for the special 'unspecified' address (0.0.0.0).
    ///
    /// This property is defined in _UNIX Network Programming, Second Edition_,
    /// W. Richard Stevens, p. 891; see also [ip7].
    ///
    /// [ip7]: http://man7.org/linux/man-pages/man7/ip.7.html
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// assert_eq!(Ipv4Addr::new(0, 0, 0, 0).is_unspecified(), true);
    /// assert_eq!(Ipv4Addr::new(45, 22, 13, 197).is_unspecified(), false);
    /// ```
    pub fn is_unspecified(&self) -> bool {
        self.inner.iter().all(|b| *b == 0)
    }

    /// Returns [`true`] if this is a loopback address (127.0.0.0/8).
    ///
    /// This property is defined by [IETF RFC 1122].
    ///
    /// [IETF RFC 1122]: https://tools.ietf.org/html/rfc1122
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// assert_eq!(Ipv4Addr::new(127, 0, 0, 1).is_loopback(), true);
    /// assert_eq!(Ipv4Addr::new(45, 22, 13, 197).is_loopback(), false);
    /// ```
    pub fn is_loopback(&self) -> bool {
        self.inner[0] == 127
    }

    /// Returns [`true`] if this is a private address.
    ///
    /// The private address ranges are defined in [IETF RFC 1918] and include:
    ///
    ///  - 10.0.0.0/8
    ///  - 172.16.0.0/12
    ///  - 192.168.0.0/16
    ///
    /// [IETF RFC 1918]: https://tools.ietf.org/html/rfc1918
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// assert_eq!(Ipv4Addr::new(10, 0, 0, 1).is_private(), true);
    /// assert_eq!(Ipv4Addr::new(10, 10, 10, 10).is_private(), true);
    /// assert_eq!(Ipv4Addr::new(172, 16, 10, 10).is_private(), true);
    /// assert_eq!(Ipv4Addr::new(172, 29, 45, 14).is_private(), true);
    /// assert_eq!(Ipv4Addr::new(172, 32, 0, 2).is_private(), false);
    /// assert_eq!(Ipv4Addr::new(192, 168, 0, 2).is_private(), true);
    /// assert_eq!(Ipv4Addr::new(192, 169, 0, 2).is_private(), false);
    /// ```
    pub fn is_private(&self) -> bool {
        match (self.inner[0], self.inner[1]) {
            (10, _) => true,
            (172, b) if 16 <= b && b <= 31 => true,
            (192, 168) => true,
            _ => false,
        }
    }

    /// Returns [`true`] if the address is link-local (169.254.0.0/16).
    ///
    /// This property is defined by [IETF RFC 3927].
    ///
    /// [IETF RFC 3927]: https://tools.ietf.org/html/rfc3927
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// assert_eq!(Ipv4Addr::new(169, 254, 0, 0).is_link_local(), true);
    /// assert_eq!(Ipv4Addr::new(169, 254, 10, 65).is_link_local(), true);
    /// assert_eq!(Ipv4Addr::new(16, 89, 10, 65).is_link_local(), false);
    /// ```
    pub fn is_link_local(&self) -> bool {
        self.inner[0] == 169 && self.inner[1] == 254
    }

    /// Returns [`true`] if the address appears to be globally routable.
    /// See [iana-ipv4-special-registry][ipv4-sr].
    ///
    /// The following return false:
    ///
    /// - private address (10.0.0.0/8, 172.16.0.0/12 and 192.168.0.0/16)
    /// - the loopback address (127.0.0.0/8)
    /// - the link-local address (169.254.0.0/16)
    /// - the broadcast address (255.255.255.255/32)
    /// - test addresses used for documentation (192.0.2.0/24, 198.51.100.0/24 and 203.0.113.0/24)
    /// - the unspecified address (0.0.0.0)
    ///
    /// [ipv4-sr]: https://www.iana.org/assignments/iana-ipv4-special-registry/iana-ipv4-special-registry.xhtml
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// fn main() {
    ///     assert_eq!(Ipv4Addr::new(10, 254, 0, 0).is_global(), false);
    ///     assert_eq!(Ipv4Addr::new(192, 168, 10, 65).is_global(), false);
    ///     assert_eq!(Ipv4Addr::new(172, 16, 10, 65).is_global(), false);
    ///     assert_eq!(Ipv4Addr::new(0, 0, 0, 0).is_global(), false);
    ///     assert_eq!(Ipv4Addr::new(80, 9, 12, 3).is_global(), true);
    /// }
    /// ```
    pub fn is_global(&self) -> bool {
        !self.is_private()
            && !self.is_loopback()
            && !self.is_link_local()
            && !self.is_broadcast()
            && !self.is_documentation()
            && !self.is_unspecified()
    }

    /// Returns [`true`] if this is a multicast address (224.0.0.0/4).
    ///
    /// Multicast addresses have a most significant octet between 224 and 239,
    /// and is defined by [IETF RFC 5771].
    ///
    /// [IETF RFC 5771]: https://tools.ietf.org/html/rfc5771
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// assert_eq!(Ipv4Addr::new(224, 254, 0, 0).is_multicast(), true);
    /// assert_eq!(Ipv4Addr::new(236, 168, 10, 65).is_multicast(), true);
    /// assert_eq!(Ipv4Addr::new(172, 16, 10, 65).is_multicast(), false);
    /// ```
    pub fn is_multicast(&self) -> bool {
        self.inner[0] & 0xF0 == 0xE0
    }

    /// Returns [`true`] if this is a broadcast address (255.255.255.255).
    ///
    /// A broadcast address has all octets set to 255 as defined in [IETF RFC 919].
    ///
    /// [IETF RFC 919]: https://tools.ietf.org/html/rfc919
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// assert_eq!(Ipv4Addr::new(255, 255, 255, 255).is_broadcast(), true);
    /// assert_eq!(Ipv4Addr::new(236, 168, 10, 65).is_broadcast(), false);
    /// ```
    pub fn is_broadcast(&self) -> bool {
        self.inner.iter().all(|b| *b == 255)
    }

    /// Returns [`true`] if this address is in a range designated for documentation.
    ///
    /// This is defined in [IETF RFC 5737]:
    ///
    /// - 192.0.2.0/24 (TEST-NET-1)
    /// - 198.51.100.0/24 (TEST-NET-2)
    /// - 203.0.113.0/24 (TEST-NET-3)
    ///
    /// [IETF RFC 5737]: https://tools.ietf.org/html/rfc5737
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv4Addr;
    ///
    /// assert_eq!(Ipv4Addr::new(192, 0, 2, 255).is_documentation(), true);
    /// assert_eq!(Ipv4Addr::new(198, 51, 100, 65).is_documentation(), true);
    /// assert_eq!(Ipv4Addr::new(203, 0, 113, 6).is_documentation(), true);
    /// assert_eq!(Ipv4Addr::new(193, 34, 17, 19).is_documentation(), false);
    /// ```
    pub fn is_documentation(&self) -> bool {
        match (self.inner[0], self.inner[1], self.inner[2]) {
            (192, 0, 2) => true,
            (198, 51, 100) => true,
            (203, 0, 113) => true,
            _ => false,
        }
    }

    /// Converts this address to an IPv4-compatible [IPv6 address].
    ///
    /// a.b.c.d becomes ::a.b.c.d
    ///
    /// [IPv6 address]: ../../no-std-net/struct.Ipv6Addr.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{Ipv4Addr, Ipv6Addr};
    ///
    /// assert_eq!(Ipv4Addr::new(192, 0, 2, 255).to_ipv6_compatible(),
    ///            Ipv6Addr::new(0, 0, 0, 0, 0, 0, 49152, 767));
    /// ```
    pub fn to_ipv6_compatible(&self) -> Ipv6Addr {
        Ipv6Addr::new(
            0,
            0,
            0,
            0,
            0,
            0,
            ((self.inner[0] as u16) << 8) | self.inner[1] as u16,
            ((self.inner[2] as u16) << 8) | self.inner[3] as u16,
        )
    }

    /// Converts this address to an IPv4-mapped [IPv6 address].
    ///
    /// a.b.c.d becomes ::ffff:a.b.c.d
    ///
    /// [IPv6 address]: ../../no-std-net/struct.Ipv6Addr.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{Ipv4Addr, Ipv6Addr};
    ///
    /// assert_eq!(Ipv4Addr::new(192, 0, 2, 255).to_ipv6_mapped(),
    ///            Ipv6Addr::new(0, 0, 0, 0, 0, 65535, 49152, 767));
    /// ```
    pub fn to_ipv6_mapped(&self) -> Ipv6Addr {
        Ipv6Addr::new(
            0,
            0,
            0,
            0,
            0,
            0xffff,
            ((self.inner[0] as u16) << 8) | self.inner[1] as u16,
            ((self.inner[2] as u16) << 8) | self.inner[3] as u16,
        )
    }
}

impl ::fmt::Display for Ipv4Addr {
    fn fmt(&self, fmt: &mut ::fmt::Formatter) -> ::fmt::Result {
        write!(
            fmt,
            "{}.{}.{}.{}",
            self.inner[0], self.inner[1], self.inner[2], self.inner[3]
        )
    }
}

impl ::fmt::Debug for Ipv4Addr {
    fn fmt(&self, fmt: &mut ::fmt::Formatter) -> ::fmt::Result {
        ::fmt::Display::fmt(self, fmt)
    }
}

impl PartialEq<Ipv4Addr> for IpAddr {
    fn eq(&self, other: &Ipv4Addr) -> bool {
        match *self {
            IpAddr::V4(ref v4) => v4 == other,
            IpAddr::V6(_) => false,
        }
    }
}

impl PartialEq<IpAddr> for Ipv4Addr {
    fn eq(&self, other: &IpAddr) -> bool {
        match *other {
            IpAddr::V4(ref v4) => self == v4,
            IpAddr::V6(_) => false,
        }
    }
}

impl PartialOrd<Ipv4Addr> for IpAddr {
    fn partial_cmp(&self, other: &Ipv4Addr) -> Option<Ordering> {
        match *self {
            IpAddr::V4(ref v4) => v4.partial_cmp(other),
            IpAddr::V6(_) => Some(Ordering::Greater),
        }
    }
}

impl PartialOrd<IpAddr> for Ipv4Addr {
    fn partial_cmp(&self, other: &IpAddr) -> Option<Ordering> {
        match *other {
            IpAddr::V4(ref v4) => self.partial_cmp(v4),
            IpAddr::V6(_) => Some(Ordering::Less),
        }
    }
}

impl From<Ipv4Addr> for u32 {
    fn from(ip: Ipv4Addr) -> u32 {
        u32::from_be_bytes(ip.inner)
    }
}

impl From<u32> for Ipv4Addr {
    fn from(ip: u32) -> Ipv4Addr {
        Ipv4Addr {
            inner: u32::to_be_bytes(ip),
        }
    }
}

impl Ipv6Addr {
    /// Creates a new IPv6 address from eight 16-bit segments.
    ///
    /// The result will represent the IP address a:b:c:d:e:f:g:h.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// let addr = Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff);
    /// ```
    #[allow(clippy::many_single_char_names)]
    pub fn new(a: u16, b: u16, c: u16, d: u16, e: u16, f: u16, g: u16, h: u16) -> Ipv6Addr {
        Ipv6Addr {
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

    /// Creates a new IPv6 address representing localhost: `::1`.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// let addr = Ipv6Addr::localhost();
    /// assert_eq!(addr, Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1));
    /// ```
    pub fn localhost() -> Ipv6Addr {
        Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1)
    }

    /// Creates a new IPv6 address representing the unspecified address: `::`
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// let addr = Ipv6Addr::unspecified();
    /// assert_eq!(addr, Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0));
    /// ```
    pub fn unspecified() -> Ipv6Addr {
        Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0)
    }

    /// Returns the first 16-bit segment that makes up this address.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// assert_eq!(Ipv6Addr::new(0x0011, 0x2233, 0, 0, 0, 0, 0, 0).first_segment(), 0x11);
    /// ```
    pub fn first_segment(&self) -> u16 {
        (self.inner[0] as u16) << 8 | (self.inner[1] as u16)
    }

    /// Returns the second 16-bit segment that makes up this address.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// assert_eq!(Ipv6Addr::new(0x0011, 0x2233, 0, 0, 0, 0, 0, 0).second_segment(), 0x2233);
    /// ```
    pub fn second_segment(&self) -> u16 {
        (self.inner[2] as u16) << 8 | (self.inner[3] as u16)
    }

    /// Returns the eight 16-bit segments that make up this address.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).segments(),
    ///            [0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff]);
    /// ```
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

    /// Returns [`true`] for the special 'unspecified' address (::).
    ///
    /// This property is defined in [IETF RFC 4291].
    ///
    /// [IETF RFC 4291]: https://tools.ietf.org/html/rfc4291
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_unspecified(), false);
    /// assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0).is_unspecified(), true);
    /// ```
    pub fn is_unspecified(&self) -> bool {
        self.inner.iter().all(|b| *b == 0)
    }

    /// Returns [`true`] if this is a loopback address (::1).
    ///
    /// This property is defined in [IETF RFC 4291].
    ///
    /// [IETF RFC 4291]: https://tools.ietf.org/html/rfc4291
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_loopback(), false);
    /// assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0x1).is_loopback(), true);
    /// ```
    pub fn is_loopback(&self) -> bool {
        self.segments() == [0, 0, 0, 0, 0, 0, 0, 1]
    }

    /// Returns [`true`] if the address appears to be globally routable.
    ///
    /// The following return [`false`]:
    ///
    /// - the loopback address
    /// - link-local, site-local, and unique local unicast addresses
    /// - interface-, link-, realm-, admin- and site-local multicast addresses
    ///
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    /// [`false`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// fn main() {
    ///     assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_global(), true);
    ///     assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0x1).is_global(), false);
    ///     assert_eq!(Ipv6Addr::new(0, 0, 0x1c9, 0, 0, 0xafc8, 0, 0x1).is_global(), true);
    /// }
    /// ```
    pub fn is_global(&self) -> bool {
        match self.multicast_scope() {
            Some(Ipv6MulticastScope::Global) => true,
            None => self.is_unicast_global(),
            _ => false,
        }
    }

    /// Returns [`true`] if this is a unique local address (fc00::/7).
    ///
    /// This property is defined in [IETF RFC 4193].
    ///
    /// [IETF RFC 4193]: https://tools.ietf.org/html/rfc4193
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// fn main() {
    ///     assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_unique_local(),
    ///                false);
    ///     assert_eq!(Ipv6Addr::new(0xfc02, 0, 0, 0, 0, 0, 0, 0).is_unique_local(), true);
    /// }
    /// ```
    pub fn is_unique_local(&self) -> bool {
        self.inner[0] & 0xfe == 0xfc
    }

    /// Returns [`true`] if the address is unicast and link-local (fe80::/10).
    ///
    /// This property is defined in [IETF RFC 4291].
    ///
    /// [IETF RFC 4291]: https://tools.ietf.org/html/rfc4291
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// fn main() {
    ///     assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_unicast_link_local(),
    ///                false);
    ///     assert_eq!(Ipv6Addr::new(0xfe8a, 0, 0, 0, 0, 0, 0, 0).is_unicast_link_local(), true);
    /// }
    /// ```
    pub fn is_unicast_link_local(&self) -> bool {
        self.first_segment() & 0xffc0 == 0xfe80
    }

    /// Returns [`true`] if this is a deprecated unicast site-local address
    /// (fec0::/10).
    ///
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// fn main() {
    ///     assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_unicast_site_local(),
    ///                false);
    ///     assert_eq!(Ipv6Addr::new(0xfec2, 0, 0, 0, 0, 0, 0, 0).is_unicast_site_local(), true);
    /// }
    /// ```
    pub fn is_unicast_site_local(&self) -> bool {
        self.first_segment() & 0xffc0 == 0xfec0
    }

    /// Returns [`true`] if this is an address reserved for documentation
    /// (2001:db8::/32).
    ///
    /// This property is defined in [IETF RFC 3849].
    ///
    /// [IETF RFC 3849]: https://tools.ietf.org/html/rfc3849
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// fn main() {
    ///     assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_documentation(),
    ///                false);
    ///     assert_eq!(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0).is_documentation(), true);
    /// }
    /// ```
    pub fn is_documentation(&self) -> bool {
        self.first_segment() == 0x2001 && self.second_segment() == 0xdb8
    }

    /// Returns [`true`] if the address is a globally routable unicast address.
    ///
    /// The following return false:
    ///
    /// - the loopback address
    /// - the link-local addresses
    /// - the (deprecated) site-local addresses
    /// - unique local addresses
    /// - the unspecified address
    /// - the address range reserved for documentation
    ///
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// fn main() {
    ///     assert_eq!(Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 0).is_unicast_global(), false);
    ///     assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_unicast_global(),
    ///                true);
    /// }
    /// ```
    pub fn is_unicast_global(&self) -> bool {
        !self.is_multicast()
            && !self.is_loopback()
            && !self.is_unicast_link_local()
            && !self.is_unicast_site_local()
            && !self.is_unique_local()
            && !self.is_unspecified()
            && !self.is_documentation()
    }

    /// Returns the address's multicast scope if the address is multicast.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{Ipv6Addr, Ipv6MulticastScope};
    ///
    /// fn main() {
    ///     assert_eq!(Ipv6Addr::new(0xff0e, 0, 0, 0, 0, 0, 0, 0).multicast_scope(),
    ///                              Some(Ipv6MulticastScope::Global));
    ///     assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).multicast_scope(), None);
    /// }
    /// ```
    pub fn multicast_scope(&self) -> Option<Ipv6MulticastScope> {
        if self.is_multicast() {
            match self.inner[1] & 0x0F {
                1 => Some(Ipv6MulticastScope::InterfaceLocal),
                2 => Some(Ipv6MulticastScope::LinkLocal),
                3 => Some(Ipv6MulticastScope::RealmLocal),
                4 => Some(Ipv6MulticastScope::AdminLocal),
                5 => Some(Ipv6MulticastScope::SiteLocal),
                8 => Some(Ipv6MulticastScope::OrganizationLocal),
                14 => Some(Ipv6MulticastScope::Global),
                _ => None,
            }
        } else {
            None
        }
    }

    /// Returns [`true`] if this is a multicast address (ff00::/8).
    ///
    /// This property is defined by [IETF RFC 4291].
    ///
    /// [IETF RFC 4291]: https://tools.ietf.org/html/rfc4291
    /// [`true`]: https://doc.rust-lang.org/std/primitive.bool.html
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// assert_eq!(Ipv6Addr::new(0xff00, 0, 0, 0, 0, 0, 0, 0).is_multicast(), true);
    /// assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).is_multicast(), false);
    /// ```
    pub fn is_multicast(&self) -> bool {
        self.inner[0] == 0xff
    }

    /// Converts this address to an [IPv4 address]. Returns [`None`] if this address is
    /// neither IPv4-compatible or IPv4-mapped.
    ///
    /// ::a.b.c.d and ::ffff:a.b.c.d become a.b.c.d
    ///
    /// [IPv4 address]: ../../no-std-net/struct.Ipv4Addr.html
    /// [`None`]: https://doc.rust-lang.org/core/option/enum.Option.html#variant.None
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::{Ipv4Addr, Ipv6Addr};
    ///
    /// assert_eq!(Ipv6Addr::new(0xff00, 0, 0, 0, 0, 0, 0, 0).to_ipv4(), None);
    /// assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, 0xc00a, 0x2ff).to_ipv4(),
    ///            Some(Ipv4Addr::new(192, 10, 2, 255)));
    /// assert_eq!(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1).to_ipv4(),
    ///            Some(Ipv4Addr::new(0, 0, 0, 1)));
    /// ```
    pub fn to_ipv4(&self) -> Option<Ipv4Addr> {
        match self.segments() {
            [0, 0, 0, 0, 0, f, g, h] if f == 0 || f == 0xffff => Some(Ipv4Addr::new(
                (g >> 8) as u8,
                g as u8,
                (h >> 8) as u8,
                h as u8,
            )),
            _ => None,
        }
    }

    /// Returns the sixteen eight-bit integers the IPv6 address consists of.
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// assert_eq!(Ipv6Addr::new(0xff00, 0, 0, 0, 0, 0, 0, 0).octets(),
    ///            [255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    /// ```
    pub fn octets(&self) -> [u8; 16] {
        self.inner.clone()
    }
}

impl ::fmt::Display for Ipv6Addr {
    #[allow(clippy::many_single_char_names)]
    fn fmt(&self, fmt: &mut ::fmt::Formatter) -> ::fmt::Result {
        match self.segments() {
            // We need special cases for :: and ::1, otherwise they're formatted
            // as ::0.0.0.[01]
            [0, 0, 0, 0, 0, 0, 0, 0] => write!(fmt, "::"),
            [0, 0, 0, 0, 0, 0, 0, 1] => write!(fmt, "::1"),
            // Ipv4 Compatible address
            [0, 0, 0, 0, 0, 0, g, h] => write!(
                fmt,
                "::{}.{}.{}.{}",
                (g >> 8) as u8,
                g as u8,
                (h >> 8) as u8,
                h as u8
            ),
            // Ipv4-Mapped address
            [0, 0, 0, 0, 0, 0xffff, g, h] => write!(
                fmt,
                "::ffff:{}.{}.{}.{}",
                (g >> 8) as u8,
                g as u8,
                (h >> 8) as u8,
                h as u8
            ),
            _ => {
                fn find_zero_slice(segments: &[u16; 8]) -> (usize, usize) {
                    let mut longest_span_len = 0;
                    let mut longest_span_at = 0;
                    let mut cur_span_len = 0;
                    let mut cur_span_at = 0;

                    for i in 0..8 {
                        if segments[i] == 0 {
                            if cur_span_len == 0 {
                                cur_span_at = i;
                            }

                            cur_span_len += 1;

                            if cur_span_len > longest_span_len {
                                longest_span_len = cur_span_len;
                                longest_span_at = cur_span_at;
                            }
                        } else {
                            cur_span_len = 0;
                            cur_span_at = 0;
                        }
                    }

                    (longest_span_at, longest_span_len)
                }

                let (zeros_at, zeros_len) = find_zero_slice(&self.segments());

                if zeros_len > 1 {
                    fn fmt_subslice(segments: &[u16], fmt: &mut ::fmt::Formatter) -> ::fmt::Result {
                        if !segments.is_empty() {
                            write!(fmt, "{:x}", segments[0])?;
                            for &seg in &segments[1..] {
                                write!(fmt, ":{:x}", seg)?;
                            }
                        }
                        Ok(())
                    }

                    fmt_subslice(&self.segments()[..zeros_at], fmt)?;
                    fmt.write_str("::")?;
                    fmt_subslice(&self.segments()[zeros_at + zeros_len..], fmt)
                } else {
                    let &[a, b, c, d, e, f, g, h] = &self.segments();
                    write!(
                        fmt,
                        "{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}",
                        a, b, c, d, e, f, g, h
                    )
                }
            }
        }
    }
}

impl ::fmt::Debug for Ipv6Addr {
    fn fmt(&self, fmt: &mut ::fmt::Formatter) -> ::fmt::Result {
        ::fmt::Display::fmt(self, fmt)
    }
}

impl PartialEq<IpAddr> for Ipv6Addr {
    fn eq(&self, other: &IpAddr) -> bool {
        match *other {
            IpAddr::V4(_) => false,
            IpAddr::V6(ref v6) => self == v6,
        }
    }
}

impl PartialEq<Ipv6Addr> for IpAddr {
    fn eq(&self, other: &Ipv6Addr) -> bool {
        match *self {
            IpAddr::V4(_) => false,
            IpAddr::V6(ref v6) => v6 == other,
        }
    }
}

impl PartialOrd<Ipv6Addr> for IpAddr {
    fn partial_cmp(&self, other: &Ipv6Addr) -> Option<Ordering> {
        match *self {
            IpAddr::V4(_) => Some(Ordering::Less),
            IpAddr::V6(ref v6) => v6.partial_cmp(other),
        }
    }
}

impl PartialOrd<IpAddr> for Ipv6Addr {
    fn partial_cmp(&self, other: &IpAddr) -> Option<Ordering> {
        match *other {
            IpAddr::V4(_) => Some(Ordering::Greater),
            IpAddr::V6(ref v6) => self.partial_cmp(v6),
        }
    }
}

impl From<Ipv6Addr> for u128 {
    /// Convert an `Ipv6Addr` into a host byte order `u128`.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// let addr = Ipv6Addr::new(
    ///     0x1020, 0x3040, 0x5060, 0x7080,
    ///     0x90A0, 0xB0C0, 0xD0E0, 0xF00D,
    /// );
    /// assert_eq!(0x102030405060708090A0B0C0D0E0F00D_u128, u128::from(addr));
    /// ```
    fn from(ip: Ipv6Addr) -> u128 {
        u128::from_be_bytes(ip.inner)
    }
}

impl From<u128> for Ipv6Addr {
    /// Convert a host byte order `u128` into an `Ipv6Addr`.
    ///
    /// # Examples
    ///
    /// ```
    /// use no_std_net::Ipv6Addr;
    ///
    /// let addr = Ipv6Addr::from(0x102030405060708090A0B0C0D0E0F00D_u128);
    /// assert_eq!(
    ///     Ipv6Addr::new(
    ///         0x1020, 0x3040, 0x5060, 0x7080,
    ///         0x90A0, 0xB0C0, 0xD0E0, 0xF00D,
    ///     ),
    ///     addr);
    /// ```
    fn from(ip: u128) -> Ipv6Addr {
        Ipv6Addr {
            inner: u128::to_be_bytes(ip),
        }
    }
}

// TODO: add module tests here!
