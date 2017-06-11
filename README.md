![Kodi logo](https://raw.githubusercontent.com/xbmc/xbmc-forum/master/xbmc/images/logo-sbs-black.png)

![Google Summer of Code logo](https://developers.google.com/open-source/gsoc/resources/downloads/GSoC-logo-horizontal-200.png)

# Kodi fork for Google Summer of Code 2017 project: Wayland Support

This is my fork of Kodi where I will be coding on my Google Summer of Code project of adding (back) Wayland support.

Feel free to participate in discussions either directly here on GitHub (issues/pull requests) or the [Kodi community forum thread](http://forum.kodi.tv/showthread.php?tid=309254) and report bugs or features that you would like to see integrated.

## Compiling with Wayland

To build Kodi with the Wayland windowing system backend, please roughly follow `docs/README.linux` with the following exceptions.

Before actually compiling, you will additionally need to

* install your distribution's develoment packages for Wayland, e.g. `libwayland-dev` on Debian/Ubuntu, and the `wayland-protocols` package and
* build and install waylandpp from https://github.com/pkerling/waylandpp/ - please follow the build instructions in the repository.

To enable Wayland, you must run CMake with `-DCORE_PLATFORM_NAME=wayland`, i.e. to build run

    $ mkdir kodi-build && cd kodi-build
    $ cmake .. -DCORE_PLATFORM_NAME=wayland -DCMAKE_INSTALL_PREFIX=/usr/local
    $ cmake --build .

## Useful links

* [Original proposal and discussion thread in Kodi community forums](http://forum.kodi.tv/showthread.php?tid=309254&pid=2552143#pid2552143)
* [Project page at Google Summer of Code homepage](https://summerofcode.withgoogle.com/projects/#4913542374359040)

* [Kodi wiki](http://kodi.wiki/)
* [Kodi bug tracker](http://trac.kodi.tv)
* [Kodi community forums](http://forum.kodi.tv/)
* [Kodi website](http://kodi.tv)

## Legal

Portions of this page are reproduced from work created and [shared by Google](https://developers.google.com/readme/policies/) and used according to terms described in the [Creative Commons Attribution-Noncommercial-No Derivative Works 3.0 Unported License](http://creativecommons.org/licenses/by-nc-nd/3.0/).
