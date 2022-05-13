extern crate proc_macro;
use proc_macro::TokenStream;

use quote::quote;

/// Init the kernel module for running all the tests
///
/// ## Usage
///
/// ```
/// fn test_0() {}
///
/// fn test_1() {}
///
/// [krdma_test(test_0,test_1)]
/// fn init() {
///
/// }
/// ```
/// Note that the `init` function will be called *first* before all the other functions.
///
#[proc_macro_attribute]
pub fn krdma_test(args: TokenStream, item: TokenStream) -> TokenStream {

    let input = syn::parse_macro_input!(item as syn::ItemFn);

    let args = syn::parse_macro_input!(args as syn::AttributeArgs);
    let ret = &input.sig.output;
    let name = &input.sig.ident;
    let body = &input.block;

    quote! {

    struct Module;

    fn #name() #ret {
        #body
    }

    impl linux_kernel_module::KernelModule for Module {

        fn init() -> linux_kernel_module::KernelResult<Self> {
            linux_kernel_module::println!("in krdma test framework test functions");

            #name(); // the init function
            #(#args());*; // the unit test functions
            Ok(Self {})
        }
    }

    linux_kernel_module::kernel_module!(
        Module,
        author: b"xmm",
        description: b"Test test framework in the kernel",
        license: b"GPL"
    );

    }.into()
}

/// Init the kernel module for running a single function
///
/// ## Usage
///
/// ```
/// [krdma_module]
/// fn test_0() {}
///
/// ```
///
#[proc_macro_attribute]
pub fn krdma_main(_args: TokenStream, item: TokenStream) -> TokenStream {
    let input = syn::parse_macro_input!(item as syn::ItemFn);

    let ret = &input.sig.output;
    let name = &input.sig.ident;
    let body = &input.block;

    quote! {

    fn #name() #ret {
        #body
    }

    struct Module;

    impl linux_kernel_module::KernelModule for Module {

        fn init() -> linux_kernel_module::KernelResult<Self> {
            linux_kernel_module::println!("in krdma test framework");
            #name();
            Ok(Self {})
        }
    }

    linux_kernel_module::kernel_module!(
        Module,
        author: b"xmm",
        description: b"Test test framework in the kernel",
        license: b"GPL"
    );

    }
    .into()
}

/// Install a drop function for the kernel module
///
/// ## Usage
///
/// ```
/// #[krdma_drop]
/// fn drop() {
///    /* Clean some resources here */
/// }
/// ```
///
#[proc_macro_attribute]
pub fn krdma_drop(_args: TokenStream, item: TokenStream) -> TokenStream {
    let input = syn::parse_macro_input!(item as syn::ItemFn);

    let ret = &input.sig.output;
    let name = &input.sig.ident;
    let body = &input.block;

    quote!{
    
    fn #name() #ret {
        #body
    }

    impl Drop for Module {
        fn drop(&mut self) {
            #name();
            linux_kernel_module::println!("krdma test framework dropped");
        }
    }
    
    }
    .into()
}
