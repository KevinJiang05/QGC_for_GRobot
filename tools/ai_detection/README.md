# QGC AI Detection Bridge

This folder contains small external tools for stage 2 of the QGC video AI overlay.
QGC listens for detection JSON on UDP `57610`; the detector reads an RTSP/video
source, runs YOLO, and sends boxes to QGC.

## Install

Use the shared YOLO environment:

```powershell
python -m venv D:\Develop\envs\yolo
D:\Develop\envs\yolo\Scripts\python.exe -m pip install --upgrade pip
D:\Develop\envs\yolo\Scripts\python.exe -m pip install -r tools\ai_detection\requirements.txt
```

## Run YOLO

Start QGC first, configure the same video source in QGC, then run:

```powershell
D:\Develop\envs\yolo\Scripts\python.exe tools\ai_detection\yolo_to_qgc_udp.py --source "rtsp://user:pass@192.168.1.10:8554/live" --model D:\Develop\envs\yolo\models\yolov8n.pt
```

On Windows, the batch wrapper uses the shared environment and default model:

```powershell
.\StartYoloToQGC.cmd "rtsp://user:pass@192.168.1.10:8554/live"
```

Other useful examples:

```powershell
D:\Develop\envs\yolo\Scripts\python.exe tools\ai_detection\yolo_to_qgc_udp.py --source 0 --model D:\Develop\envs\yolo\models\yolov8n.pt
D:\Develop\envs\yolo\Scripts\python.exe tools\ai_detection\yolo_to_qgc_udp.py --source .\sample.mp4 --model .\runs\detect\train\weights\best.pt --conf 0.35
```

## Test Without YOLO

Use the sample sender to verify that QGC receives UDP data and draws boxes:

```powershell
D:\Develop\envs\yolo\Scripts\python.exe tools\ai_detection\send_sample_detection.py
```

## JSON Format

QGC accepts normalized boxes:

```json
{
  "timestamp": 1710000000.123,
  "detections": [
    { "label": "target", "confidence": 0.91, "x": 0.2, "y": 0.15, "w": 0.3, "h": 0.4 }
  ]
}
```

It also accepts pixel-space `bbox` values when `frame_width` and `frame_height`
are present:

```json
{
  "timestamp": 1710000000.123,
  "frame_width": 1280,
  "frame_height": 720,
  "detections": [
    { "label": "target", "confidence": 0.91, "bbox": [256, 108, 640, 396] }
  ]
}
```
