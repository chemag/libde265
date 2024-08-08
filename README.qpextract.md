# libde265 qpextract

A tool to extract QP values from an h265 file in raw bitstream mode (Annex B),
or a stream with NAL units.


# Operation

(1) build from the qpextract branch
```
$ git clone --branch qpextract https://github.com/chemag/libde265
$ cd libde265
$ ./autogen.sh
$ ./configure --enable-sherlock265
$ make -j
```

(2) analyze an Annex B file
```
$ tools/qpextract -i tears_400_x265.h265  | csvcut -c frame,qp_num,qp_min,qp_max,qp_avg,qp_stddev,qpw_num,qpw_min,qpw_max,qpw_avg,qpw_stddev | csvlook -I
| frame | qp_num | qp_min | qp_max | qp_avg    | qp_stddev | qpw_num | qpw_min | qpw_max | qpw_avg   | qpw_stddev |
| ----- | ------ | ------ | ------ | --------- | --------- | ------- | ------- | ------- | --------- | ---------- |
| 0     | 1563   | 29     | 34     | 30.872681 | 1.282208  | 1536000 | 29      | 34      | 31.115833 | 1.307701   |
| 1     | 981    | 32     | 40     | 33.895005 | 1.199807  | 1536000 | 32      | 40      | 34.074583 | 1.204825   |
| 2     | 567    | 38     | 41     | 38.941799 | 0.915359  | 1536000 | 38      | 41      | 38.864500 | 0.880420   |
| 3     | 486    | 39     | 42     | 40.518519 | 0.995187  | 1536000 | 39      | 42      | 40.447000 | 1.019407   |
| 4     | 459    | 39     | 42     | 40.366013 | 0.927091  | 1536000 | 39      | 42      | 40.319000 | 0.934473   |
| 5     | 438    | 39     | 42     | 40.826484 | 1.041176  | 1536000 | 39      | 42      | 40.813333 | 1.076331   |
| 6     | 1029   | 32     | 40     | 34.125364 | 1.218910  | 1536000 | 32      | 40      | 34.409875 | 1.411073   |
...
| 332   | 480    | 38     | 41     | 40.372917 | 0.998173  | 1536000 | 38      | 41      | 40.549333 | 0.933577   |
| 333   | 495    | 38     | 41     | 39.846465 | 1.236100  | 1536000 | 38      | 41      | 39.978333 | 1.228223   |
| 334   | 444    | 38     | 41     | 40.238739 | 1.139675  | 1536000 | 38      | 41      | 40.478667 | 1.004097   |
```

Each line contains the frame ID, and the distribution of the QP values for the frame (number, min, max, avg, stddev). Values are both non-weighted (all CUs weighted the same) and weighted (CUs are weighted by their size, so the QP for a 64x64 CU is considered 4x times in the average when compared to a 32x32 CU)).

For example, the second line corresponds to frame 1, and has 981 CUs, with an average of 33.89 QP value. The full histogram is also in the output (removed here).


# Other Options

The default CLI arguments will produce the distribution of the luminance QP values. The tool can also produce the distribution of:
* chrominance (Cb or Cr) QP values
* prediction modes
* CTU mode (plain distribution of CUs)

See the help output:
```
$ tools/qpextract --help
# qpextract  v1.0.3
usage: /home/chemag/proj/libde265/tools/.libs/qpextract [options] -i videofile.bin [-o output.csv]
The video file must be a raw bitstream, or a stream with NAL units (option -n).

options:
  -c, --check-hash  perform hash check
  -n, --nal         input is a stream with 4-byte length prefixed NAL units
  -d, --dump        dump headers
  -T, --highest-TID select highest temporal sublayer to decode
      --disable-deblocking   disable deblocking filter
      --disable-sao          disable sample-adaptive offset filter
  -q, --min-qp      minimum QP for CSV dump
  -Q, --max-qp      maximum QP for CSV dump
  --qpymode         QPY mode (get the distribution of QP Y values)
  --qpcbmode        QPCb mode (get the distribution of QP Cb values)
  --qpcrmode        QPCr mode (get the distribution of QP Cr values)
  -p, --predmode    pred mode (get the distribution of prediction modes)
  --ctumode         ctu mode (get the distribution of CTUs)
  --fullmode        full mode (get full QP, pred, CTU info)
  -h, --help        show help
```

Go back to the [main libde265 page](README.md).
