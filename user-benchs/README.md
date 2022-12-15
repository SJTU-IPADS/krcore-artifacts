# Benchmark the performance of KRCore under user-space 

## Cargo descriptions

- `bench_server`: server-side code for the RDMA evaluations 
- `bench_rdma`: client-side code for the RDMA evaluations


## Build

Note: please read the build script file `scripts/build.toml` and modify the fields accordingly.

```
python3 ../exp_scripts/bootstrap.py -f scripts/build.toml -u YOUR NAME -p YOUR_PASSWORD
```

Be sure to copy the binaries to the machines that run the test with: 

```
python3 ../exp_scripts/bootstrap.py -f scripts/copy_bins.toml -u YOUR NAME -p YOUR_PASSWORD
```

## Run 

Be sure the configurations in the `scripts/run_basic.toml` match the one with the above build process. 

```
python3 ../exp_scripts/bootstrap.py -f scripts/run_basic.toml -u YOUR NAME -p YOUR_PASSWORD
```
