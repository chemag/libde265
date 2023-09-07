# libde265 qpextract

A tool to extract QP values from an h265 file in raw bitstream mode (Annex B),
or a stream with NAL units.


# Operation

```
$ tools/qpextract input_file.265
id: 0 qp_distro[26:36] { 84 143 164 199 272 409 323 557 455 664 330 }
id: 1 qp_distro[35:41] { 5 0 0 1 11 1448 2135 }
id: 2 qp_distro[38:43] { 12 0 0 0 1697 1891 }
id: 3 qp_distro[41:44] { 21 1 362 3216 }
id: 4 qp_distro[44:46] { 20 2436 1144 }
id: 5 qp_distro[46:47] { 1850 1750 }
id: 6 qp_distro[47:48] { 380 3220 }
id: 7 qp_distro[48:49] { 1043 2557 }
id: 8 qp_distro[49:49] { 3600 }
id: 9 qp_distro[49:49] { 3600 }
id: 10 qp_distro[49:49] { 3600 }
id: 11 qp_distro[49:50] { 712 2888 }
id: 12 qp_distro[49:50] { 670 2930 }
```

Each line contains the frame ID, and the distribution of the QP values for the fame. The second line (`id: 1 qp_distro[35:41] { 5 0 0 1 11 1448 2135 }`) corresponds to frame 1, and has 5x 35 QP values, 1x 38 QP values, 11x 39 QP values, 1148 40 QP values, and 2135x 41 QP values.

Go back to the [main libde265 page](README.md).
