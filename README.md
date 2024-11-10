# hello-wayland-extra

Purpose of this repo is to implement some common client features, in the most mininal way.

Features like:

- text rendering (using cairo)
- server side decorations, (using xdg-decoration-unstable protocol)
- (TODO) client side decorations

## Dependencies

The following dependencies are required for the Makefile to function properly:

- libwayland
- wayland-protocols
- ImageMagick

## License

MIT

[xdg-shell]: https://gitlab.freedesktop.org/wayland/wayland-protocols/-/tree/master/stable/xdg-shell
