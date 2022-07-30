#[macro_export(local_inner_macros)]
macro_rules! log {
    ($lvl:expr, $($arg:tt)+) => ({
        let lvl = $lvl;
        if lvl <= $crate::STATIC_MAX_LEVEL && lvl <= $crate::max_level() {
            $crate::println!("{}@{}: [{:5}] - {}", __log_file!(), __log_line!(),
            lvl,
             __log_format_args!($($arg)+));
        }
    });
}

#[macro_export(local_inner_macros)]
macro_rules! error {
    (target: $target:expr, $($arg:tt)+) => (
        log!(target: $target, $crate::level::Level::Error, $($arg)+)
    );
    ($($arg:tt)+) => (
        log!($crate::level::Level::Error, $($arg)+)
    )
}

#[macro_export(local_inner_macros)]
macro_rules! warn {
    (target: $target:expr, $($arg:tt)+) => (
        log!(target: $target, $crate::level::Level::Warn, $($arg)+)
    );
    ($($arg:tt)+) => (
        log!($crate::level::Level::Warn, $($arg)+)
    )
}

#[macro_export(local_inner_macros)]
macro_rules! info {
    /* 
    (target: $target:expr, $($arg:tt)+) => (
        log!(target: $target, $crate::level::Level::Info, $($arg)+)
    ); */
    ($($arg:tt)+) => (
        log!($crate::level::Level::Info, $($arg)+)
    )
}

#[macro_export(local_inner_macros)]
macro_rules! debug {
    (target: $target:expr, $($arg:tt)+) => (
        log!(target: $target, $crate::level::Level::Debug, $($arg)+)
    );
    ($($arg:tt)+) => (
        log!($crate::level::Level::Debug, $($arg)+)
    )
}

#[doc(hidden)]
#[macro_export]
macro_rules! __log_format_args {
    ($($args:tt)*) => {
        format_args!($($args)*)
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! __log_file {
    () => {
        file!()
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! __log_line {
    () => {
        line!()
    };
}

