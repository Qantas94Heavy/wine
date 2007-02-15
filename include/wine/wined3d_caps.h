/*
 * Copyright 2007 Henri Verbeet
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_WINED3D_CAPS_H
#define __WINE_WINED3D_CAPS_H

#define WINED3DLINECAPS_TEXTURE                             0x00000001
#define WINED3DLINECAPS_ZTEST                               0x00000002
#define WINED3DLINECAPS_BLEND                               0x00000004
#define WINED3DLINECAPS_ALPHACMP                            0x00000008
#define WINED3DLINECAPS_FOG                                 0x00000010

#define WINED3DPBLENDCAPS_ZERO                              0x00000001
#define WINED3DPBLENDCAPS_ONE                               0x00000002
#define WINED3DPBLENDCAPS_SRCCOLOR                          0x00000004
#define WINED3DPBLENDCAPS_INVSRCCOLOR                       0x00000008
#define WINED3DPBLENDCAPS_SRCALPHA                          0x00000010
#define WINED3DPBLENDCAPS_INVSRCALPHA                       0x00000020
#define WINED3DPBLENDCAPS_DESTALPHA                         0x00000040
#define WINED3DPBLENDCAPS_INVDESTALPHA                      0x00000080
#define WINED3DPBLENDCAPS_DESTCOLOR                         0x00000100
#define WINED3DPBLENDCAPS_INVDESTCOLOR                      0x00000200
#define WINED3DPBLENDCAPS_SRCALPHASAT                       0x00000400
#define WINED3DPBLENDCAPS_BOTHSRCALPHA                      0x00000800
#define WINED3DPBLENDCAPS_BOTHINVSRCALPHA                   0x00001000
#define WINED3DPBLENDCAPS_BLENDFACTOR                       0x00002000

#define WINED3DPCMPCAPS_NEVER                               0x00000001
#define WINED3DPCMPCAPS_LESS                                0x00000002
#define WINED3DPCMPCAPS_EQUAL                               0x00000004
#define WINED3DPCMPCAPS_LESSEQUAL                           0x00000008
#define WINED3DPCMPCAPS_GREATER                             0x00000010
#define WINED3DPCMPCAPS_NOTEQUAL                            0x00000020
#define WINED3DPCMPCAPS_GREATEREQUAL                        0x00000040
#define WINED3DPCMPCAPS_ALWAYS                              0x00000080

#define WINED3DPMISCCAPS_MASKZ                              0x00000002
#define WINED3DPMISCCAPS_LINEPATTERNREP                     0x00000004
#define WINED3DPMISCCAPS_CULLNONE                           0x00000010
#define WINED3DPMISCCAPS_CULLCW                             0x00000020
#define WINED3DPMISCCAPS_CULLCCW                            0x00000040
#define WINED3DPMISCCAPS_COLORWRITEENABLE                   0x00000080
#define WINED3DPMISCCAPS_CLIPPLANESCALEDPOINTS              0x00000100
#define WINED3DPMISCCAPS_CLIPTLVERTS                        0x00000200
#define WINED3DPMISCCAPS_TSSARGTEMP                         0x00000400
#define WINED3DPMISCCAPS_BLENDOP                            0x00000800
#define WINED3DPMISCCAPS_NULLREFERENCE                      0x00001000
#define WINED3DPMISCCAPS_INDEPENDENTWRITEMASKS              0x00004000
#define WINED3DPMISCCAPS_PERSTAGECONSTANT                   0x00008000
#define WINED3DPMISCCAPS_FOGANDSPECULARALPHA                0x00010000
#define WINED3DPMISCCAPS_SEPARATEALPHABLEND                 0x00020000
#define WINED3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS            0x00040000
#define WINED3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING         0x00080000
#define WINED3DPMISCCAPS_FOGVERTEXCLAMPED                   0x00100000

#define WINED3DPTADDRESSCAPS_WRAP                           0x00000001
#define WINED3DPTADDRESSCAPS_MIRROR                         0x00000002
#define WINED3DPTADDRESSCAPS_CLAMP                          0x00000004
#define WINED3DPTADDRESSCAPS_BORDER                         0x00000008
#define WINED3DPTADDRESSCAPS_INDEPENDENTUV                  0x00000010
#define WINED3DPTADDRESSCAPS_MIRRORONCE                     0x00000020

#define WINED3DSTENCILCAPS_KEEP                             0x00000001
#define WINED3DSTENCILCAPS_ZERO                             0x00000002
#define WINED3DSTENCILCAPS_REPLACE                          0x00000004
#define WINED3DSTENCILCAPS_INCRSAT                          0x00000008
#define WINED3DSTENCILCAPS_DECRSAT                          0x00000010
#define WINED3DSTENCILCAPS_INVERT                           0x00000020
#define WINED3DSTENCILCAPS_INCR                             0x00000040
#define WINED3DSTENCILCAPS_DECR                             0x00000080
#define WINED3DSTENCILCAPS_TWOSIDED                         0x00000100

#define WINED3DTEXOPCAPS_DISABLE                            0x00000001
#define WINED3DTEXOPCAPS_SELECTARG1                         0x00000002
#define WINED3DTEXOPCAPS_SELECTARG2                         0x00000004
#define WINED3DTEXOPCAPS_MODULATE                           0x00000008
#define WINED3DTEXOPCAPS_MODULATE2X                         0x00000010
#define WINED3DTEXOPCAPS_MODULATE4X                         0x00000020
#define WINED3DTEXOPCAPS_ADD                                0x00000040
#define WINED3DTEXOPCAPS_ADDSIGNED                          0x00000080
#define WINED3DTEXOPCAPS_ADDSIGNED2X                        0x00000100
#define WINED3DTEXOPCAPS_SUBTRACT                           0x00000200
#define WINED3DTEXOPCAPS_ADDSMOOTH                          0x00000400
#define WINED3DTEXOPCAPS_BLENDDIFFUSEALPHA                  0x00000800
#define WINED3DTEXOPCAPS_BLENDTEXTUREALPHA                  0x00001000
#define WINED3DTEXOPCAPS_BLENDFACTORALPHA                   0x00002000
#define WINED3DTEXOPCAPS_BLENDTEXTUREALPHAPM                0x00004000
#define WINED3DTEXOPCAPS_BLENDCURRENTALPHA                  0x00008000
#define WINED3DTEXOPCAPS_PREMODULATE                        0x00010000
#define WINED3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR             0x00020000
#define WINED3DTEXOPCAPS_MODULATECOLOR_ADDALPHA             0x00040000
#define WINED3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR          0x00080000
#define WINED3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA          0x00100000
#define WINED3DTEXOPCAPS_BUMPENVMAP                         0x00200000
#define WINED3DTEXOPCAPS_BUMPENVMAPLUMINANCE                0x00400000
#define WINED3DTEXOPCAPS_DOTPRODUCT3                        0x00800000
#define WINED3DTEXOPCAPS_MULTIPLYADD                        0x01000000
#define WINED3DTEXOPCAPS_LERP                               0x02000000

#endif /* __WINE_WINED3D_CAPS_H */
