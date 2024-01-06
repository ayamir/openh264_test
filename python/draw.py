import argparse
import cmder
import os
import sys
import re
import typing
import matplotlib
import matplotlib.pyplot as plt
from matplotlib.pyplot import figure
from utils import get_dirs_path_in_path
from bjontegaard_metric import BD_RATE

matplotlib.use("Agg")
figure(figsize=(18, 6), dpi=192)

plt.rcParams.update({"font.size": 18})
plt.rcParams["font.family"] = "Times New Roman"

testbinDir = os.path.join(os.getcwd(), "..", "testbin")
videoScale = "1824x1920"
videoFps = "60.0"
pixFmt = "yuv420p"
baseFile = "out.mp4"
diffFile = "out-diff.mp4"

def getRecordDirs() -> typing.Tuple[typing.List[str], typing.List[str]]:
    dirpaths = get_dirs_path_in_path(testbinDir)
    # remove __pycache__ dir
    dirpaths = dirpaths[:-1]
    dirRes = []
    for dirpath in dirpaths:
        # remove dirs which name is not end with m
        if dirpath[-1] != "m":
            continue
        dirRes.append(dirpath)
    # sort by bits
    dirRes.sort(key=lambda s: float(s.split(os.path.sep)[-1].split("m")[0]))
    bitsRes = []
    for dirpath in dirRes:
        bits = dirpath.split(os.path.sep)[-1].split("m")[0]
        bitsRes.append(bits)
    return dirRes, bitsRes


def convert2mp4(isRemove: bool = False) -> None:
    dirpaths, _ = getRecordDirs()
    cmder.infOut("Converting h264 to mp4...")
    for dirpath in dirpaths:
        if os.path.isfile(os.path.join(dirpath, baseFile)):
            if isRemove:
                cmder.runCmd(f"rm -f {dirpath}/{baseFile}", False)
                cmder.runCmd(
                    f"ffmpeg -r {videoFps} -i {dirpath}/out.h264 -c:v copy -c:a copy {dirpath}/{baseFile}",
                    False,
                )
        else:
            cmder.runCmd(
                f"ffmpeg -r {videoFps} -i {dirpath}/out.h264 -c:v copy -c:a copy {dirpath}/{baseFile}",
                False,
            )

        if os.path.isfile(os.path.join(dirpath, diffFile)):
            if isRemove:
                cmder.runCmd(f"rm -f {dirpath}/{diffFile}", False)
                cmder.runCmd(
                    f"ffmpeg -r {videoFps} -i {dirpath}/out-diff.h264 -c:v copy -c:a copy {dirpath}/{diffFile}",
                    False,
                )
        else:
            cmder.runCmd(
                f"ffmpeg -r {videoFps} -i {dirpath}/out-diff.h264 -c:v copy -c:a copy {dirpath}/{diffFile}",
                False,
            )
    cmder.successOut("Done.")


def calculateMetric(metric: str, distort: str, ref: str) -> str | None:
    if metric != "ssim" and metric != "psnr" and metric != "vmaf":
        cmder.redStr("Unknown metric: {}".format(metric))
        return None

    if metric == "ssim":
        pattern = r"All:(\d+.\d+)"
        command = f"ffmpeg -nostdin -nostats -y -threads 8 \
            -i {distort} -i {ref} \
            -lavfi ssim='stats_file=ssim.log' -f null -"
    elif metric == "psnr":
        pattern = r"average:(\d+.\d+)"
        command = f"ffmpeg -nostdin -nostats -y -threads 8 \
            -i {distort} -i {ref} \
            -filter_complex psnr -f null -"
    else:
        pattern = r"VMAF score: (\d+.\d+)"
        command = f"ffmpeg -nostdin -nostats -y -threads 16 \
            -i {distort} -i {ref} \
            -lavfi \"libvmaf=model_path=../testbin/vmaf_v0.6.1.json\" -f null -"

    _, res = cmder.runCmd(command, True)
    match = re.search(pattern, res)
    if match:
        return match.group(1)


def compareMetric(
    metric: str, bitsLevel: str, withFile: str, withoutFile: str, ref: str
) -> None:
    withScore = calculateMetric(metric, withFile, ref)
    withoutScore = calculateMetric(metric, withoutFile, ref)
    res = f"BitsLevel={bitsLevel}: withScore={withScore}, withoutScore={withoutScore}"
    with open(f"{metric}.txt", "a") as f:
        f.write(res + "\n")
    cmder.successOut(res)


def calculate(metrics: list) -> None:
    refFile = os.path.join(testbinDir, "ref.mp4")
    recordDirs, bits = getRecordDirs()
    for metric in metrics:
        if metric != "ssim" and metric != "psnr" and metric != "vmaf":
            cmder.redStr("Unknown metric: {}".format(metric))
            return
        cmder.infOut(f"Calculating {metric}...")
        if os.path.isfile(f"{metric}.txt"):
            os.remove(f"{metric}.txt")
        for i in range(len(recordDirs)):
            recordDir = recordDirs[i]
            withFile = os.path.join(recordDir, diffFile)
            withoutFile = os.path.join(recordDir, baseFile)
            compareMetric(metric, bits[i], withFile, withoutFile, refFile)


def parseScore(metric: str, bitrates: list, withs: list, withouts: list) -> None:
    if metric != "ssim" and metric != "psnr" and metric != "vmaf":
        return

    fileName = ""
    pattern = r"BitsLevel=(\d+.\d+): withScore=(\d+.\d+), withoutScore=(\d+.\d+)"
    if metric == "ssim":
        fileName = "ssim.txt"
    elif metric == "psnr":
        fileName = "psnr.txt"
    else:
        fileName = "vmaf.txt"

    with open(fileName, "r") as f:
        # iterate all line in file
        for line in f:
            match = re.search(pattern, line)
            if match:
                bitrates.append(float(match.group(1)))
                withs.append(float(match.group(2)))
                withouts.append(float(match.group(3)))
            else:
                cmder.redStr("Parse error: {}".format(line))
                return


def drawFigure(metric: str, format: str) -> None:
    if metric != "ssim" and metric != "psnr" and metric != "vmaf":
        return
    if format != "png" and format != "pdf" and format != "svg":
        return
    outfile = f"{metric}.{format}"
    markerSize = 15

    bitrates = []
    withs = []
    withouts = []
    parseScore(metric, bitrates, withs, withouts)
    plt.plot(
        bitrates,
        withouts,
        linestyle="dashed",
        label="Base",
        marker="^",
        color="blue",
        markersize=markerSize,
        zorder=1,
    )
    plt.plot(
        bitrates,
        withs,
        linestyle="solid",
        label="VRmate",
        marker="o",
        color="red",
        markersize=markerSize,
        zorder=2,
    )

    plt.xlabel("Bitrate (Mbps)")
    if metric == "ssim":
        plt.ylim(top=0.9995)
        plt.ylabel("SSIM")
    elif metric == "psnr":
        plt.ylim(top=60)
        plt.ylabel("PSNR (dB)")
    else:
        plt.ylim(top=97.5)
        plt.ylabel("VMAF")
    plt.tick_params(direction="in")
    plt.grid(True, linestyle="-.", color="gray", linewidth="0.5", axis="both")
    plt.gca().spines["top"].set_color("none")
    plt.gca().spines["right"].set_color("none")
    plt.gca().spines["bottom"].set_linewidth(1.5)
    plt.gca().spines["left"].set_linewidth(1.5)
    plt.gca().set_axisbelow(True)
    plt.legend(loc="upper center", bbox_to_anchor=(0.5, 1.15), ncol=2)
    plt.savefig(outfile)
    plt.clf()
    cmder.successOut(f"{outfile} saved")


def drawFigures(format: str) -> None:
    if format != "png" and format != "pdf" and format != "svg":
        return
    outfile = f"all.{format}"

    i = 1
    for metric in ["psnr", "ssim", "vmaf"]:
        bitrates = []
        withs = []
        withouts = []
        parseScore(metric, bitrates, withs, withouts)
        plt.subplot(1, 3, i)
        plt.plot(
            bitrates,
            withouts,
            linewidth=2,
            linestyle="dashed",
            label="Baseline",
            marker="^",
            color="blue",
            markersize="10",
            zorder=1,
        )
        plt.plot(
            bitrates,
            withs,
            linewidth=2,
            linestyle="solid",
            label="VRmate",
            marker="o",
            color="red",
            markersize="10",
            zorder=2,
        )
        plt.xlabel("Bitrate (Mbps)")
        if metric == "ssim":
            plt.ylim(top=0.9995)
            plt.ylabel("SSIM")
        elif metric == "psnr":
            plt.ylim(top=60)
            plt.ylabel("PSNR (dB)")
        else:
            plt.ylim(top=97.5)
            plt.ylabel("VMAF")
        plt.tick_params(direction="in")
        plt.grid(True, linestyle="-.", color="gray", linewidth="0.5", axis="both")
        plt.gca().spines["top"].set_color("none")
        plt.gca().spines["right"].set_color("none")
        plt.gca().spines["bottom"].set_linewidth(1.5)
        plt.gca().spines["left"].set_linewidth(1.5)
        plt.gca().set_axisbelow(True)
        plt.legend(loc="upper center", bbox_to_anchor=(0.5, 1.15), ncol=2)
        i += 1

    plt.tight_layout()
    plt.subplots_adjust(
        left=0.05, right=0.95, top=0.85, bottom=0.15, wspace=0.3, hspace=0.3
    )
    plt.savefig(outfile)
    cmder.successOut(f"{outfile} saved")


# return BD-Rate (type: float)
def calculateBDRate(metric: str) -> float:
    bitrates = []
    withs = []
    withouts = []
    parseScore(metric, bitrates, withs, withouts)
    BDRate = BD_RATE(bitrates, withouts, bitrates, withs)
    return BDRate


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-m", "--metrics", nargs="+", help="Metrics to calcute and draw"
    )
    parser.add_argument(
        "-c", "--calculate", action="store_true", help="Calculate metrics"
    )
    parser.add_argument("-b", "--bdrate", action="store_true", help="Calculate BD-Rate")
    parser.add_argument("-d", "--draw", action="store_true", help="Draw figure")
    parser.add_argument("-f", "--format", help="Figure format")
    args, _ = parser.parse_known_args()
    metrics = args.metrics
    format = str(args.format)
    print(metrics)
    if ("-c" in sys.argv) or ("--calculate" in sys.argv) and metrics != []:
        # convert2mp4()
        calculate(metrics)

    if ("-d" in sys.argv) or ("--draw" in sys.argv) and format != "":
        if metrics != None and metrics != []:
            for metric in metrics:
                drawFigure(metric, format)
        else:
            drawFigures(format)

    if ("-b" in sys.argv) or ("--bdrate" in sys.argv) and metrics != []:
        for metric in metrics:
            print("BD-Rate: {}".format(calculateBDRate(metric)))
