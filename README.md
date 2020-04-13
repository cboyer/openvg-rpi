# OpenVG tests with Raspberry Pi

Experimentations with OpenVG for hardware accelerated 2D drawing on Raspberry Pi (zero W).
Use PbufferSurce, PbufferFromClientBuffer and direct on-screen rendering.
Use Freetype2 library for text drawing with TrueType fonts.
Pictures are saved to PNG files with stb library.

### Performances

Raspberry Pi Zero W on Raspbian GNU/Linux 10 (buster), kernel 4.19.97.
Image size: 1920x1080.
Draws: ellipse + round rectangle + 2 strings.

<table>
<tr>
<td></td><td colspan="2">without pixels reading</td><td colspan="2">with pixels reading</td>
</tr>
<tr>
<td></td><td>VgFinish</td><td>VgFlush</td><td>VgFinish</td><td>VgFlush</td>
</tr>
<tr>
<td>onscreen.c</td><td>93.75 i/s, 9.5% CPU</td><td>166.66 i/s; 29% CPU</td><td>24.19 i/s, 39% CPU</td><td>25.21 i/s, 40% CPU</td>
</tr>
<tr>
<td>PbufferSurface.c</td><td>96.77 i/s, 10.5% CPU</td><td>176.47 i/s; 30% CPU</td><td>20.68 i/s, 26.5% CPU</td><td>23.07 i/s, 33% CPU</td>
</tr>
<tr>
<td>PbufferFromClientBuffer.c</td><td>56.60 i/s, 6.5% CPU</td><td>68.18 i/s; 7.8% CPU</td><td>11.95 i/s, 24% CPU</td><td>10.04 i/s, 20% CPU</td>
</tr>
</table>


### Compilation

```Console
gcc PbufferSurface.c -O2 -Wall -Werror -L/opt/vc/lib -lbrcmEGL -lbrcmGLESv2 -lbcm_host -I/opt/vc/include -I/opt/vc/include/interface/vmcs_host/linux -I/opt/vc/include/interface/vcos/pthreads  -I/usr/include/stb -lstb -I/usr/include/freetype2 -lfreetype -I/usr/include/glib-2.0 -I/usr/lib/arm-linux-gnueabihf/glib-2.0/include -lglib-2.0
```

### Official documentation
[OpenVG Specifications](https://www.khronos.org/registry/OpenVG/specs/openvg-1.1.pdf)
[OpenVG quick reference](https://www.khronos.org/files/openvg-quick-reference-card.pdf)
