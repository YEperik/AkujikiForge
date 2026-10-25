#!/usr/bin/env python3
"""AkuijikiForge native desktop interface (PySide6; no HTML UI)."""
from __future__ import annotations

import math
import os
import shutil
import sys
from pathlib import Path

from PySide6.QtCore import QProcess, Qt, QUrl
from PySide6.QtGui import QColor, QPainter, QPen, QTextCursor
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QFileDialog, QFrame, QHBoxLayout, QLabel,
    QMainWindow, QMessageBox, QPushButton, QPlainTextEdit,     QSizePolicy, QSlider, QStackedWidget, QToolButton, QVBoxLayout, QWidget,
)
from PySide6.QtMultimedia import QAudioOutput, QMediaPlayer
from PySide6.QtMultimediaWidgets import QVideoWidget

try:
    from model_data import MeshData, load_mesh
except ImportError:
    from ui.model_data import MeshData, load_mesh

APP_ROOT = Path(__file__).resolve().parents[1]

APP_QSS = r"""
QWidget { background: #10151d; color: #e8edf5; font-family: 'Segoe UI', 'Inter', sans-serif; font-size: 10pt; }
QMainWindow { background: #10151d; }
QFrame#Sidebar { background: #0b1017; border-right: 1px solid #242d3a; }
QLabel#Brand { font-size: 17pt; font-weight: 700; color: #f7f9fc; padding: 8px 4px; }
QLabel#Eyebrow { color: #62d9c4; font-size: 9pt; font-weight: 700; letter-spacing: 1px; }
QLabel#PageTitle { font-size: 23pt; font-weight: 700; color: #f7f9fc; }
QLabel#SubTitle { color: #aab6c7; font-size: 10pt; }
QFrame#Card { background: #171f2b; border: 1px solid #2a3647; border-radius: 13px; }
QLabel#CardTitle { color: #f4f7fb; font-size: 12pt; font-weight: 700; }
QLabel#Muted { color: #aab6c7; }
QToolButton#NavButton { color: #b6c1d0; background: transparent; border: 0; border-radius: 8px; padding: 12px 14px; text-align: left; }
QToolButton#NavButton:hover { background: #192331; color: #ffffff; }
QToolButton#NavButton:checked { background: #193b42; color: #7ce4d1; font-weight: 700; }
QPushButton { background: #253244; color: #f4f7fb; border: 1px solid #3a4b62; border-radius: 8px; padding: 9px 13px; }
QPushButton:hover { background: #31445c; }
QPushButton#Primary { background: #39b9a7; color: #071b1a; font-weight: 700; border: 0; }
QPushButton#Primary:hover { background: #58d3c0; }
QLineEdit, QPlainTextEdit { background: #0c1118; color: #e8edf5; border: 1px solid #2a3647; border-radius: 8px; padding: 8px; selection-background-color: #296b66; }
QCheckBox { color: #c7d0dd; spacing: 8px; }
QSlider::groove:horizontal { height: 5px; background: #2a3647; border-radius: 2px; }
QSlider::handle:horizontal { width: 13px; margin: -5px 0; background: #62d9c4; border-radius: 6px; }
QProgressBar { background: #222e3d; border: 0; height: 5px; border-radius: 2px; }
QProgressBar::chunk { background: #62d9c4; border-radius: 2px; }
"""


def card(title: str, body: str) -> QFrame:
    frame = QFrame()
    frame.setObjectName("Card")
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(18, 16, 18, 16)
    layout.setSpacing(8)
    heading = QLabel(title)
    heading.setObjectName("CardTitle")
    description = QLabel(body)
    description.setObjectName("Muted")
    description.setWordWrap(True)
    layout.addWidget(heading)
    layout.addWidget(description)
    return frame


class ModelCanvas(QWidget):
    """Lightweight native 3D wireframe/point preview; no browser or web engine."""
    def __init__(self) -> None:
        super().__init__()
        self.setMinimumSize(460, 360)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.vertices: list[tuple[float, float, float]] = []
        self.faces: list[tuple[int, ...]] = []
        self.name = "No model loaded"
        self.yaw = -0.65
        self.pitch = 0.45
        self.zoom = 1.0
        self._drag_position = None
        self._center = (0.0, 0.0, 0.0)
        self._extent = 1.0

    def set_mesh(self, mesh: MeshData, name: str) -> None:
        self.vertices = mesh.vertices
        self.faces = mesh.faces
        self.name = name
        axes = list(zip(*self.vertices))
        self._center = tuple((min(axis) + max(axis)) / 2.0 for axis in axes)
        self._extent = max(max(axis) - min(axis) for axis in axes) or 1.0
        self.yaw, self.pitch, self.zoom = -0.65, 0.45, 1.0
        self.update()

    def _rotate(self, point: tuple[float, float, float]) -> tuple[float, float, float]:
        x, y, z = (point[i] - self._center[i] for i in range(3))
        cy, sy = math.cos(self.yaw), math.sin(self.yaw)
        x, z = x * cy - z * sy, x * sy + z * cy
        cp, sp = math.cos(self.pitch), math.sin(self.pitch)
        y, z = y * cp - z * sp, y * sp + z * cp
        return x, y, z

    def _project(self, point: tuple[float, float, float]) -> tuple[float, float, float]:
        x, y, z = self._rotate(point)
        scale = min(self.width(), self.height()) * 0.66 / self._extent * self.zoom
        perspective = 1.0 / max(0.55, 1.0 + z / self._extent * 0.12)
        return self.width() / 2 + x * scale * perspective, self.height() / 2 - y * scale * perspective, z

    def paintEvent(self, _event) -> None:  # noqa: N802 - Qt callback
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.fillRect(self.rect(), QColor("#0b1017"))
        painter.setPen(QPen(QColor("#1e2938"), 1))
        spacing = 36
        for x in range(0, self.width(), spacing): painter.drawLine(x, 0, x, self.height())
        for y in range(0, self.height(), spacing): painter.drawLine(0, y, self.width(), y)
        painter.setPen(QColor("#aab6c7"))
        painter.drawText(16, 24, self.name)
        if not self.vertices:
            painter.setPen(QColor("#718096"))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "Open an OBJ or ASCII PLY file to preview geometry")
            return

        projected = [self._project(v) for v in self.vertices]
        if self.faces:
            edge_set: set[tuple[int, int]] = set()
            for face in self.faces:
                for idx, a in enumerate(face):
                    b = face[(idx + 1) % len(face)]
                    edge_set.add((min(a, b), max(a, b)))
            painter.setPen(QPen(QColor("#3a7180"), 1))
            for a, b in edge_set:
                pa, pb = projected[a], projected[b]
                painter.drawLine(round(pa[0]), round(pa[1]), round(pb[0]), round(pb[1]))
        else:
            painter.setPen(QPen(QColor("#62d9c4"), 3))
            for x, y, _z in projected:
                painter.drawPoint(round(x), round(y))
        painter.setPen(QColor("#92a1b5"))
        painter.drawText(16, self.height() - 16,
                         f"{len(self.vertices):,} vertices  ·  {len(self.faces):,} faces  ·  drag to rotate / wheel to zoom")

    def mousePressEvent(self, event) -> None:  # noqa: N802
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_position = event.position().toPoint()

    def mouseMoveEvent(self, event) -> None:  # noqa: N802
        if self._drag_position is not None and event.buttons() & Qt.MouseButton.LeftButton:
            current = event.position().toPoint()
            delta = current - self._drag_position
            self.yaw += delta.x() * 0.008
            self.pitch = max(-1.5, min(1.5, self.pitch + delta.y() * 0.008))
            self._drag_position = current
            self.update()

    def mouseReleaseEvent(self, _event) -> None:  # noqa: N802
        self._drag_position = None

    def wheelEvent(self, event) -> None:  # noqa: N802
        factor = 1.12 if event.angleDelta().y() > 0 else 1 / 1.12
        self.zoom = max(0.2, min(8.0, self.zoom * factor))
        self.update()


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("AkuijikiForge — Asset Workbench")
        self.resize(1240, 820)
        self.setMinimumSize(980, 660)
        self.process = QProcess(self)
        self.process.readyReadStandardOutput.connect(self._read_process_output)
        self.process.readyReadStandardError.connect(self._read_process_error)
        self.process.finished.connect(self._process_finished)
        self.converter_process = QProcess(self)
        self.converter_process.readyReadStandardOutput.connect(self._read_converter_output)
        self.converter_process.readyReadStandardError.connect(self._read_converter_error)
        self.converter_process.finished.connect(self._converter_finished)
        self._cli_path: str | None = self._find_cli()
        self._seek_is_dragging = False

        root = QWidget()
        root_layout = QHBoxLayout(root)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(0)
        sidebar = QFrame()
        sidebar.setObjectName("Sidebar")
        sidebar.setFixedWidth(235)
        side_layout = QVBoxLayout(sidebar)
        side_layout.setContentsMargins(18, 24, 18, 18)
        side_layout.setSpacing(8)
        brand = QLabel("AkuijikiForge")
        brand.setObjectName("Brand")
        side_layout.addWidget(brand)
        eyebrow = QLabel("ASSET WORKBENCH")
        eyebrow.setObjectName("Eyebrow")
        side_layout.addWidget(eyebrow)
        side_layout.addSpacing(24)

        self.pages = QStackedWidget()
        self.nav_buttons: list[QToolButton] = []
        for index, (label, page_factory) in enumerate([
            ("Overview", self._overview_page),
            ("File Inspector", self._inspector_page),
            ("3D Preview", self._model_page),
            ("Media Player", self._media_page),
        ]):
            page = page_factory()
            self.pages.addWidget(page)
            button = QToolButton()
            button.setObjectName("NavButton")
            button.setText(label)
            button.setCheckable(True)
            button.setAutoExclusive(True)
            button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextOnly)
            button.clicked.connect(lambda checked=False, i=index: self.pages.setCurrentIndex(i))
            side_layout.addWidget(button)
            self.nav_buttons.append(button)
        self.nav_buttons[0].setChecked(True)
        side_layout.addStretch(1)
        note = QLabel("LOCAL-FIRST\nRead-only by design\n\nNo game assets or private research are bundled.")
        note.setObjectName("Muted")
        note.setWordWrap(True)
        side_layout.addWidget(note)
        root_layout.addWidget(sidebar)
        root_layout.addWidget(self.pages, 1)
        self.setCentralWidget(root)
        self.statusBar().showMessage("Ready · Local files stay on this device")

    def _page_shell(self, eyebrow: str, title: str, subtitle: str) -> tuple[QWidget, QVBoxLayout]:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(32, 28, 32, 28)
        layout.setSpacing(16)
        small = QLabel(eyebrow.upper())
        small.setObjectName("Eyebrow")
        heading = QLabel(title)
        heading.setObjectName("PageTitle")
        heading.setWordWrap(True)
        sub = QLabel(subtitle)
        sub.setObjectName("SubTitle")
        sub.setWordWrap(True)
        layout.addWidget(small)
        layout.addWidget(heading)
        layout.addWidget(sub)
        return page, layout

    def _overview_page(self) -> QWidget:
        page, layout = self._page_shell(
            "Workspace", "Inspect assets with confidence.",
            "A native desktop toolkit for read-only file inspection, local media playback, and lightweight 3D previews."
        )
        cards = QHBoxLayout()
        cards.addWidget(card("File Inspector", "Review file size, entropy, and common file signatures without changing the source."))
        cards.addWidget(card("3D Preview", "Open local OBJ or ASCII PLY geometry and rotate the view to inspect its shape."))
        cards.addWidget(card("Media Player", "Play local audio and video through Qt Multimedia, depending on system codec support."))
        layout.addLayout(cards)
        policy = QFrame()
        policy.setObjectName("Card")
        policy_layout = QVBoxLayout(policy)
        policy_layout.setContentsMargins(20, 18, 20, 18)
        title = QLabel("Public-release boundaries")
        title.setObjectName("CardTitle")
        body = QLabel("This public build intentionally omits game-derived samples, private reverse-engineering notes, raw dumps, and detailed research exports. Load files from your own local workspace. The toolkit does not patch executables or bypass DRM / software protections.")
        body.setWordWrap(True)
        body.setObjectName("Muted")
        policy_layout.addWidget(title)
        policy_layout.addWidget(body)
        layout.addWidget(policy)
        launch = QPushButton("Open File Inspector")
        launch.setObjectName("Primary")
        launch.clicked.connect(lambda: self.pages.setCurrentIndex(1))
        launch.setFixedWidth(190)
        layout.addWidget(launch, alignment=Qt.AlignmentFlag.AlignLeft)
        layout.addStretch(1)
        return page

    def _inspector_page(self) -> QWidget:
        page, layout = self._page_shell(
            "Read-only diagnostics", "File Inspector",
            "Choose one or more local files to review basic metadata and common header signatures. No output is written to the input directory."
        )
        choose_card = QFrame(); choose_card.setObjectName("Card")
        choose_layout = QVBoxLayout(choose_card); choose_layout.setContentsMargins(18, 16, 18, 16)
        self.inspector_path = QLabel("No input file selected")
        self.inspector_path.setWordWrap(True)
        self.inspector_path.setObjectName("Muted")
        buttons = QHBoxLayout()
        browse = QPushButton("Choose file…"); browse.clicked.connect(self._choose_inspector_file)
        self.run_inspect = QPushButton("Run inspection"); self.run_inspect.setObjectName("Primary"); self.run_inspect.clicked.connect(self._run_inspector)
        self.verbose = QCheckBox("Show extended header preview")
        buttons.addWidget(browse); buttons.addWidget(self.run_inspect); buttons.addStretch(1)
        choose_layout.addWidget(self.inspector_path)
        choose_layout.addLayout(buttons)
        choose_layout.addWidget(self.verbose)
        layout.addWidget(choose_card)
        output_label = QLabel("INSPECTION OUTPUT"); output_label.setObjectName("Eyebrow")
        layout.addWidget(output_label)
        self.inspect_output = QPlainTextEdit(); self.inspect_output.setReadOnly(True); self.inspect_output.setPlaceholderText("Inspection output will appear here.")
        layout.addWidget(self.inspect_output, 1)
        binary_row = QHBoxLayout()
        self.cli_label = QLabel(f"CLI: {self._cli_path or 'not found — build with CMake or choose a binary'}")
        self.cli_label.setObjectName("Muted"); self.cli_label.setWordWrap(True)
        choose_cli = QPushButton("Choose CLI binary…"); choose_cli.clicked.connect(self._choose_cli)
        binary_row.addWidget(self.cli_label, 1); binary_row.addWidget(choose_cli)
        layout.addLayout(binary_row)
        return page

    def _model_page(self) -> QWidget:
        page, layout = self._page_shell(
            "Local geometry", "3D Preview",
            "Native Qt canvas for text-based OBJ and ASCII PLY. The preview reads only vertex/face geometry; materials, textures, and animation are not loaded."
        )
        row = QHBoxLayout()
        open_button = QPushButton("Open model…"); open_button.setObjectName("Primary"); open_button.clicked.connect(self._open_model)
        reset_button = QPushButton("Reset view"); reset_button.clicked.connect(self._reset_model_view)
        row.addWidget(open_button); row.addWidget(reset_button); row.addStretch(1)
        layout.addLayout(row)
        self.model_canvas = ModelCanvas()
        layout.addWidget(self.model_canvas, 1)
        return page

    def _media_page(self) -> QWidget:
        page, layout = self._page_shell(
            "Local playback", "Media Player",
            "Open an audio or video file stored on this device. Playback depends on the codecs available in your Qt / operating-system installation."
        )
        toolbar = QHBoxLayout()
        open_button = QPushButton("Open media…"); open_button.setObjectName("Primary"); open_button.clicked.connect(self._open_media)
        convert_button = QPushButton("Convert / extract preview…"); convert_button.clicked.connect(self._convert_media)
        self.media_name = QLabel("No media selected"); self.media_name.setObjectName("Muted"); self.media_name.setWordWrap(True)
        toolbar.addWidget(open_button); toolbar.addWidget(convert_button); toolbar.addWidget(self.media_name, 1)
        layout.addLayout(toolbar)
        video_frame = QFrame(); video_frame.setObjectName("Card")
        video_layout = QVBoxLayout(video_frame); video_layout.setContentsMargins(8, 8, 8, 8)
        self.video_widget = QVideoWidget(); self.video_widget.setMinimumHeight(270)
        self.video_widget.setStyleSheet("background: #070a0f; border-radius: 8px;")
        video_layout.addWidget(self.video_widget)
        layout.addWidget(video_frame, 1)
        self.player = QMediaPlayer(self)
        self.audio_output = QAudioOutput(self)
        self.player.setAudioOutput(self.audio_output)
        self.player.setVideoOutput(self.video_widget)
        self.player.errorOccurred.connect(self._media_error)
        self.player.durationChanged.connect(self._duration_changed)
        self.player.positionChanged.connect(self._position_changed)
        controls = QHBoxLayout()
        play = QPushButton("Play / Resume"); play.clicked.connect(self.player.play)
        pause = QPushButton("Pause"); pause.clicked.connect(self.player.pause)
        stop = QPushButton("Stop"); stop.clicked.connect(self.player.stop)
        controls.addWidget(play); controls.addWidget(pause); controls.addWidget(stop)
        self.seek = QSlider(Qt.Orientation.Horizontal); self.seek.setRange(0, 0)
        self.seek.sliderPressed.connect(lambda: setattr(self, "_seek_is_dragging", True))
        self.seek.sliderReleased.connect(self._seek_to_slider)
        controls.addWidget(self.seek, 1)
        self.media_time = QLabel("00:00 / 00:00"); self.media_time.setObjectName("Muted")
        controls.addWidget(self.media_time)
        layout.addLayout(controls)
        self.media_log = QPlainTextEdit()
        self.media_log.setReadOnly(True)
        self.media_log.setMaximumHeight(92)
        self.media_log.setPlaceholderText("Conversion and playback messages will appear here.")
        layout.addWidget(self.media_log)
        return page

    def _find_cli(self) -> str | None:
        candidates = [
            APP_ROOT / "build" / "akuijikiforge",
            APP_ROOT / "build" / "akuijikiforge.exe",
            APP_ROOT / "build" / "Release" / "akuijikiforge.exe",
            APP_ROOT / "cmake-build-release" / "akuijikiforge",
            APP_ROOT / "cmake-build-release" / "akuijikiforge.exe",
        ]
        for candidate in candidates:
            if candidate.is_file(): return str(candidate)
        return shutil.which("akuijikiforge")

    def _choose_inspector_file(self) -> None:
        files, _ = QFileDialog.getOpenFileNames(self, "Select files for inspection")
        if files:
            self._selected_files = files
            self.inspector_path.setText("\n".join(files))

    def _choose_cli(self) -> None:
        selected, _ = QFileDialog.getOpenFileName(self, "Choose AkuijikiForge CLI binary")
        if selected:
            self._cli_path = selected
            self.cli_label.setText(f"CLI: {selected}")

    def _run_inspector(self) -> None:
        selected = getattr(self, "_selected_files", [])
        if not selected:
            QMessageBox.information(self, "Choose a file", "Select at least one local file first.")
            return
        if not self._cli_path or not Path(self._cli_path).is_file():
            QMessageBox.warning(self, "CLI not found", "Build the CLI with CMake, then select the executable here.")
            return
        if self.process.state() != QProcess.ProcessState.NotRunning:
            QMessageBox.information(self, "Inspection running", "Wait for the current inspection to finish before starting another.")
            return
        self.inspect_output.clear()
        args = (["--verbose"] if self.verbose.isChecked() else []) + selected
        self.process.setProgram(self._cli_path)
        self.process.setArguments(args)
        self.process.setWorkingDirectory(str(APP_ROOT))
        self.process.start()
        self.run_inspect.setEnabled(False)
        self.statusBar().showMessage("Inspecting local files…")

    def _read_process_output(self) -> None:
        data = bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace")
        if data:
            self.inspect_output.insertPlainText(data)
            self.inspect_output.moveCursor(QTextCursor.MoveOperation.End)

    def _read_process_error(self) -> None:
        data = bytes(self.process.readAllStandardError()).decode("utf-8", errors="replace")
        if data: self.inspect_output.appendPlainText(data)

    def _process_finished(self, exit_code: int, _exit_status) -> None:
        self.run_inspect.setEnabled(True)
        self.statusBar().showMessage(f"Inspection finished · exit code {exit_code}", 6000)
        if exit_code != 0: self.inspect_output.appendPlainText(f"\nProcess exited with code {exit_code}.")

    def _open_model(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Open model", "", "Supported geometry (*.obj *.ply);;OBJ files (*.obj);;PLY files (*.ply)")
        if not path: return
        try:
            mesh = load_mesh(path)
            self.model_canvas.set_mesh(mesh, Path(path).name)
            self.statusBar().showMessage(f"Loaded {mesh.source_format}: {len(mesh.vertices):,} vertices", 5000)
        except (OSError, ValueError) as exc:
            QMessageBox.warning(self, "Cannot open model", str(exc))

    def _reset_model_view(self) -> None:
        self.model_canvas.yaw = -0.65
        self.model_canvas.pitch = 0.45
        self.model_canvas.zoom = 1.0
        self.model_canvas.update()

    def _open_media(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Open local media", "",
            "Media files (*.mp4 *.mkv *.mov *.avi *.mp3 *.wav *.flac *.ogg *.m4a *.aac);;All files (*)"
        )
        if not path: return
        self.player.stop()
        self.media_name.setText(Path(path).name)
        self.player.setSource(QUrl.fromLocalFile(os.path.abspath(path)))
        self.player.play()
        self.statusBar().showMessage("Loaded local media · playback depends on installed codecs", 6000)


    def _convert_media(self) -> None:
        source, _ = QFileDialog.getOpenFileName(self, "Choose a local media/container file")
        if not source:
            return
        destination = QFileDialog.getExistingDirectory(self, "Choose output folder")
        if not destination:
            return
        if self.converter_process.state() != QProcess.ProcessState.NotRunning:
            QMessageBox.information(self, "Conversion running", "Wait for the current conversion to finish.")
            return
        script = APP_ROOT / "tools" / "convert_media.py"
        self.media_log.clear()
        self.media_log.appendPlainText("Local media conversion log\n")
        # Use a dedicated process so conversion does not interfere with file inspection.
        self.converter_process.setProgram(sys.executable)
        self.converter_process.setArguments([str(script), "--input", source, "--out-dir", destination])
        self.converter_process.setWorkingDirectory(str(APP_ROOT))
        self.converter_process.start()
        self.statusBar().showMessage("Converting local media…")

    def _read_converter_output(self) -> None:
        data = bytes(self.converter_process.readAllStandardOutput()).decode("utf-8", errors="replace")
        if data:
            self.media_log.appendPlainText(data.rstrip())

    def _read_converter_error(self) -> None:
        data = bytes(self.converter_process.readAllStandardError()).decode("utf-8", errors="replace")
        if data:
            self.media_log.appendPlainText(data.rstrip())

    def _converter_finished(self, exit_code: int, _exit_status) -> None:
        self.statusBar().showMessage(f"Media conversion finished · exit code {exit_code}", 8000)
        self.media_log.appendPlainText(f"\nConversion exit code: {exit_code}")
        if exit_code == 0:
            QMessageBox.information(self, "Conversion complete", "Preview files were created in the selected local folder.")
        else:
            QMessageBox.warning(self, "Conversion failed", "See the conversion log on this page. Confirm FFmpeg and ffprobe are installed.")

    def _media_error(self, _error, error_string: str) -> None:
        if error_string:
            self.statusBar().showMessage(f"Media playback error: {error_string}", 10000)

    @staticmethod
    def _time_text(milliseconds: int) -> str:
        seconds = max(0, milliseconds // 1000)
        return f"{seconds // 60:02d}:{seconds % 60:02d}"

    def _duration_changed(self, duration: int) -> None:
        self.seek.setRange(0, max(0, duration))
        self._update_media_time(self.player.position(), duration)

    def _position_changed(self, position: int) -> None:
        if not self._seek_is_dragging: self.seek.setValue(position)
        self._update_media_time(position, self.player.duration())

    def _update_media_time(self, position: int, duration: int) -> None:
        self.media_time.setText(f"{self._time_text(position)} / {self._time_text(duration)}")

    def _seek_to_slider(self) -> None:
        self.player.setPosition(self.seek.value())
        self._seek_is_dragging = False


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("AkuijikiForge")
    app.setOrganizationName("AkuijikiForge")
    app.setStyle("Fusion")
    app.setStyleSheet(APP_QSS)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
