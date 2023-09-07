#!/usr/bin/env python3
"""qpparse: An h265 QP parser.

This module implements an h265 (aka h.265 aka hevc aka ISO/IEC 23008-2 MPEG-H
Part 2) QP parser. It accepts an h265 file (in Appendix B format), and
extracts the distribution of QP values used in CTBs in a per-frame basis.
Then, it creates a Figure depicting the QP distribution, where the X axis
contains the frame numbers, and the Y axis shows which QP values are more
common for each frame (using darker colors), and which are less common
(using white for "no appeareance of this QP value for this frame"). If
the number of i-frames is smaller than a threshold, it will replace
constant ticks in the X axis with the location of the I-frames.

This module depends on a tool called `qpextract`, which extracts the
QP values from the h265 file (in Appendix B format). This tool can
be found at https://github.com/chemag/libde265/
"""

import argparse
import os
import re
import subprocess
import sys

import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
from matplotlib.colors import LogNorm


IFRAME_THRESHOLD = 0.05


def run(command, **kwargs):
    debug = kwargs.get("debug", 0)
    dry_run = kwargs.get("dry_run", False)
    env = kwargs.get("env", None)
    stdin = subprocess.PIPE if kwargs.get("stdin", False) else None
    bufsize = kwargs.get("bufsize", 0)
    universal_newlines = kwargs.get("universal_newlines", False)
    default_close_fds = True if sys.platform == "linux2" else False
    close_fds = kwargs.get("close_fds", default_close_fds)
    shell = type(command) in (type(""), type(""))
    if debug > 0:
        print("running $ %s" % command)
    if dry_run:
        return 0, b"stdout", b"stderr"
    p = subprocess.Popen(  # noqa: E501
        command,
        stdin=stdin,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=bufsize,
        universal_newlines=universal_newlines,
        env=env,
        close_fds=close_fds,
        shell=shell,
    )
    # wait for the command to terminate
    if stdin is not None:
        out, err = p.communicate(stdin)
    else:
        out, err = p.communicate()
    returncode = p.returncode
    # clean up
    del p
    # return results
    return returncode, out, err


def get_qp(x, y, qps):
    r"""
    return qp and blocksize corresponding to x,y (pixels)

    >>> qps={'0,0': [1, 8], '8,0': [2, 8], '16,0': [3, 16], '32,0': [4, 32], '64,0': [5, 32], '0,8': [6, 8], '8,8': [7, 8], '0,16': [9, 16], '16,16': [10, 16], '0,32': [11, 32], '32,32': [12, 32    ], '64,32': [13, 32], '0,64': [14, 32], '32,64': [15, 32], '64,64': [16, 32]}
    >>> get_qp(0,0,qps)
    [1, 8]
    >>> get_qp(0,1,qps)
    [1, 8]
    >>> get_qp(1,0,qps)
    [1, 8]
    >>> get_qp(1,1,qps)
    [1, 8]
    >>> get_qp(9,2,qps)
    [2, 8]
    >>> get_qp(17,9,qps)
    [3, 16]
    >>> get_qp(31,15,qps)
    [3, 16]
    >>> get_qp(32,0,qps)
    [4, 32]
    >>> get_qp(32+31,0+31,qps)
    [4, 32]
    >>> get_qp(0+7,8+7,qps)
    [6, 8]
    >>> get_qp(0+15,16+15,qps)
    [9, 16]
    >>> get_qp(16+15,16+15,qps)
    [10, 16]
    >>> get_qp(32,64,qps)
    [15, 32]
    """

    def _get_qp(x, y, i):
        if i > 5:
            return None
            #'qp not found at %s,%s'%(x,y)
        s = "%s,%s" % ((x >> i) << i, (y >> i) << i)
        # print ("*************normalized %s"%s,flush=True)
        if s in qps:
            return qps[s]
        return _get_qp(x, y, i + 1)

    return _get_qp(x, y, 3)


def read_output_qp_distro(info):
    r'''
    needs libde265 patch

    >>> read_output_qp_distro("""
    ... qp_coord[0,0]: 1, CbSize: 8
    ... qp_coord[8,0]: 2, CbSize: 8
    ... qp_coord[16,0]: 3, CbSize: 16
    ... qp_coord[32,0]: 4, CbSize: 32
    ... qp_coord[64,0]: 5, CbSize: 32
    ... qp_coord[0,8]: 6, CbSize: 8
    ... qp_coord[8,8]: 7, CbSize: 8
    ... qp_coord[0,16]: 9, CbSize: 16
    ... qp_coord[16,16]: 10, CbSize: 16
    ... qp_coord[0,32]: 11, CbSize: 32
    ... qp_coord[32,32]: 12, CbSize: 32
    ... qp_coord[64,32]: 13, CbSize: 32
    ... qp_coord[0,64]: 14, CbSize: 32
    ... qp_coord[32,64]: 15, CbSize: 32
    ... qp_coord[64,64]: 16, CbSize: 32
    ... qp_coord[0,0]: 17, CbSize: 32
    ... """)
    [{'frame_number': 1, 'qps': {'0,0': [1, 8], '8,0': [2, 8], '16,0': [3, 16], '32,0': [4, 32], '64,0': [5, 32], '0,8': [6, 8], '8,8': [7, 8], '0,16': [9, 16], '16,16': [10, 16], '0,32': [11, 3    2], '32,32': [12, 32], '64,32': [13, 32], '0,64': [14, 32], '32,64': [15, 32], '64,64': [16, 32]}}, {'frame_number': 2, 'qps': {'0,0': [17, 32]}}]
    '''

    frame = {}
    frames = []
    last_yb = 0
    for item in re.findall(r"qp_coord\[(\d+),(\d+)\]: (\d+), CbSize: (\d+)", info):
        xb, yb, qp, cb = map(int, item)
        if "frame_number" not in frame:
            # print ('first line')
            frame = {"frame_number": 1, "qps": {}}
            frames.append(frame)
            last_yb = yb
        if yb < last_yb:
            # print ('new frame')
            # print ('xb %i, yb %i, qp %i, cb %i last_yb %i'%(xb, yb, qp, cb, last_yb))
            frame = {"frame_number": frame["frame_number"] + 1, "qps": {}}
            frames.append(frame)
        last_yb = yb
        frame["qps"]["%i,%i" % (xb, yb)] = [qp, cb]
    return frames


def obtain_qp_values(options, input_text):
    # process input_text
    qp_list = []
    slice_type_list = []
    min_minqp = -1
    max_maxqp = -1
    for line in input_text.splitlines():
        line = line.strip()
        # skip blank lines/comments
        if len(line) == 0 or line[0] == "#":
            continue
        # interesting lines include 'qp_distro' or 'slice_type'
        if "slice_type" in line:
            slice_type_list.append(line.split(" ")[-1])
            continue
        elif "qp_distro" in line:
            # break up the line in items
            items = re.split(r"\[|:|\]|\{|\}| ", line[line.find("qp_distro") :])
            try:
                minqp = int(items[1])
                maxqp = int(items[2])
                vals = [int(i) for i in items[3:] if i != ""]
                assert maxqp - minqp + 1 == len(vals)
            except:
                print('error in line: "%s"' % line.strip())
                continue
            # add elements to the list
            min_minqp = minqp if min_minqp == -1 else min(min_minqp, minqp)
            max_maxqp = maxqp if max_maxqp == -1 else max(max_maxqp, maxqp)
            qp_list += [[minqp, vals]]
    return qp_list, slice_type_list, min_minqp, max_maxqp


def evaluate_input(options, infile, outfile):
    # parse the h265 file
    qpextract = Runner("qpextract", path=options.qpextract_path, debug=options.debug)
    if not qpextract.exists():
        print("error: cannot find qpextract")
        exit(-1)
    ret, out, err = qpextract.call(infile)
    if ret != 0:
        print("error: qpextract failed:\n%s" % err)
        exit(-1)

    # calculate the slice_type and QP lists
    qp_list, slice_type_list, min_minqp, max_maxqp = obtain_qp_values(
        options, out.decode("utf-8")
    )
    if qp_list == []:
        print("error: no qp_distro found. '%s' should be valid 265 file" % infile)
        exit(-1)

    # make sure there are at least 3 possible QP values represented
    if min_minqp > max_maxqp - 3:
        min_minqp -= 1
        max_maxqp += 1

    # convert the list elements to a rectangular matrix
    matrix = np.empty((0, max_maxqp - min_minqp + 1), dtype=int)
    for l in qp_list:
        full_l = [
            ([0] * (l[0] - min_minqp))
            + l[1]
            + ([0] * (max_maxqp - l[0] - len(l[1]) + 1))
        ]
        matrix = np.append(matrix, full_l, axis=0)

    # fix axes
    fig, ax = plt.subplots()
    ax.matshow(
        np.flip(matrix.transpose(), 0),
        norm=LogNorm(vmin=1 + matrix.min(), vmax=1 + matrix.max()),
        cmap=plt.cm.pink.reversed(),
        aspect="auto",
    )
    fig.canvas.draw()

    # fix ylabels
    def major_formatter(max_maxqp):
        def formatter(x, pos):
            return "%i" % (max_maxqp - x)

        return ticker.FuncFormatter(formatter)

    ax.yaxis.set_major_formatter(major_formatter(max_maxqp))

    # minimize margins
    ax.margins(x=0, y=-0.25)

    # set the xticks using I-frames
    iframe_id = [i for (i, t) in enumerate(slice_type_list) if t == "I"]
    if len(iframe_id) < (IFRAME_THRESHOLD * len(slice_type_list)):
        # if number of I-frames is smaller than a threshold, replace the
        # ticks with the I-frames
        lim = ax.get_xlim()
        plt.xticks(iframe_id)
        ax.set_xlim(lim)

    # get outfile
    if outfile in (None, "-"):
        outfile = sys.stdout.buffer
    plt.savefig(outfile, bbox_inches="tight", dpi=300)


def get_options(argv):
    """Generic option parser.

    Args:
        argv: list containing arguments

    Returns:
        Namespace - An argparse.ArgumentParser-generated option object
    """
    # init parser
    # usage = 'usage: %prog [options] arg1 arg2'
    # parser = argparse.OptionParser(usage=usage)
    # parser.print_help() to get argparse.usage (large help)
    # parser.print_usage() to get argparse.usage (just usage line)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-d",
        "--debug",
        action="count",
        dest="debug",
        default=0,
        help="Increase verbosity (use multiple times for more)",
    )
    parser.add_argument(
        "--quiet", action="store_const", dest="debug", const=-1, help="Zero verbosity"
    )
    parser.add_argument(
        "--qpextract",
        action="store",
        dest="qpextract_path",
        default=None,
        metavar="QPEXTRACT_PATH",
        help="use QPEXTRACT_PATH to access qpextract",
    )
    parser.add_argument(
        "infile",
        nargs="?",
        type=str,
        default=None,
        metavar="input-file",
        help="input file",
    )
    parser.add_argument(
        "outfile",
        nargs="?",
        type=str,
        default=None,
        metavar="output-file",
        help="output file",
    )
    # do the parsing
    options = parser.parse_args(argv[1:])
    return options


def main(argv):
    # parse options
    options = get_options(argv)
    # print results
    if options.debug > 1:
        print(options)
    # do something
    evaluate_input(options, options.infile, options.outfile)


if __name__ == "__main__":
    # at least the CLI program name: (CLI) execution
    main(sys.argv)
