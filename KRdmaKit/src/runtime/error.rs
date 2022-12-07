use failure::Fail;

#[derive(Fail, Debug)]
pub enum RuntimeError {
    #[fail(display = "{}", _0)]
    Error(&'static str),
}
