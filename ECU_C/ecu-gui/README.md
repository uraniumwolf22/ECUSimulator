Custom GUI for [uraniumwolf22/ECUSimulator](https://github.com/uraniumwolf22/ECUSimulator).

> <img width="750" height="191" alt="gui-example" src="https://github.com/user-attachments/assets/fc206549-9658-41cc-ba27-517172c4f6e4" />


Building (native):
```sh
cargo build --release
```

Cross-building for `aarch64`:
```sh
RUSTFLAGS="-Clinker=aarch64-linux-gnu-gcc" cargo build --release --target aarch64-unknown-linux-gnu
```
