"""PyQt5 + PyOpenGL SKP viewer with C++ face triangulation."""
import argparse
import math
import sys
from array import array
from pathlib import Path
from PyQt5.QtCore import Qt, QTimer, QThread, pyqtSignal
from PyQt5.QtGui import QSurfaceFormat
from PyQt5.QtWidgets import (QApplication, QMainWindow, QOpenGLWidget, QFileDialog,
                             QMessageBox, QAction)
from OpenGL.GL import *
from skp_edges import EdgeReader
from skp_textures import decode_material_images

ROOT = Path(__file__).resolve().parents[1]


class LoadTask(QThread):
    loaded = pyqtSignal(object)
    failed = pyqtSignal(str)

    def __init__(self, dll, path):
        super().__init__()
        self.dll, self.path = dll, path

    def run(self):
        try:
            edges, triangles, surface = EdgeReader(self.dll).load(self.path, with_textures=True)
            decode_material_images(surface)
            self.loaded.emit((edges, triangles, surface))
        except Exception as error:
            self.failed.emit(str(error))


class EdgeView(QOpenGLWidget):
    def __init__(self):
        super().__init__()
        self.vertices = array('f')
        self.triangles = array('f')
        self.show_faces = True
        self.show_triangulation = False
        self.show_textures = True
        self.show_edges = True
        self.surface_batches = []
        self.materials = {}
        self.texture_ids = {}
        self.pending_textures = False
        self.fov_y = 45.0
        self.radius = 1.0
        self.last = None
        self.setFocusPolicy(Qt.StrongFocus)
        self.fit()

    def set_edges(self, edges, triangles=(), surface=None):
        points = [p for edge in edges for p in edge]
        self.vertices = array('f')
        self.triangles = array('f')
        self.surface_batches = []
        self.materials = surface['materials'] if surface else {}
        self.pending_textures = True
        if points:
            lo = [min(p[i] for p in points) for i in range(3)]
            hi = [max(p[i] for p in points) for i in range(3)]
            center = [(lo[i] + hi[i]) / 2 for i in range(3)]
            self.radius = max(math.sqrt(sum((hi[i] - lo[i]) ** 2 for i in range(3))) / 2, 1e-6)
            # Normalize before float conversion to keep large world coordinates stable.
            self.vertices = array('f', ((p[i] - center[i]) / self.radius for p in points for i in range(3)))
            self.triangles = array('f', ((p[i] - center[i]) / self.radius
                                        for tri in triangles for p in tri for i in range(3)))
            if surface:
                for batch in surface['batches']:
                    batch['vertices'] = array('f', ((p[i]-center[i])/self.radius
                                                    for p in batch.pop('points') for i in range(3)))
                    batch['uv'] = array('f', (v for uvq in batch.pop('uvq') for v in uvq))
                    batch['vertex_bytes'] = batch['vertices'].tobytes()
                    batch['uv_bytes'] = batch['uv'].tobytes()
                    batch['centers'] = [tuple(sum(batch['vertices'][i+j+k] for j in (0,3,6))/3
                                              for k in range(3)) for i in range(0,len(batch['vertices']),9)]
                    self.surface_batches.append(batch)
        self.fit()

    def fit(self):
        self.zoom, self.pan_x, self.pan_y = 1.0, 0.0, 0.0
        self.yaw, self.pitch = -35.0, -60.0
        self.update()

    def initializeGL(self):
        glClearColor(0.9, 0.9, 0.9, 1.0)
        glDisable(GL_LIGHTING)
        glEnable(GL_DEPTH_TEST)
        self.context().aboutToBeDestroyed.connect(self.cleanup_textures)

    def cleanup_textures(self):
        self.makeCurrent()
        if self.texture_ids:
            glDeleteTextures(list(self.texture_ids.values()))
        self.texture_ids.clear()
        self.pending_textures = True
        self.doneCurrent()

    def upload_textures(self):
        if not self.pending_textures:
            return
        if self.texture_ids:
            glDeleteTextures(list(self.texture_ids.values()))
        self.texture_ids.clear()
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1)
        max_size = int(glGetIntegerv(GL_MAX_TEXTURE_SIZE))
        for key, material in self.materials.items():
            if 'pixels' not in material:
                continue
            width, height = material['size']
            pixels = material['pixels']
            if max(width, height) > max_size:
                from PIL import Image
                picture = Image.frombytes('RGBA', (width, height), pixels)
                picture.thumbnail((max_size, max_size))
                width, height = picture.size
                pixels = picture.tobytes()
            texture = int(glGenTextures(1))
            self.texture_ids[key] = texture
            glBindTexture(GL_TEXTURE_2D, texture)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels)
        glBindTexture(GL_TEXTURE_2D, 0)
        self.pending_textures = False

    def draw_surfaces(self):
        glEnable(GL_CULL_FACE)
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        glEnable(GL_ALPHA_TEST)
        glAlphaFunc(GL_GREATER, 0.01)
        glEnableClientState(GL_VERTEX_ARRAY)
        # Opaque surfaces establish depth first. Material opacity is blended afterwards.
        opaque, translucent = [], []
        sp, cp = math.sin(math.radians(self.pitch)), math.cos(math.radians(self.pitch))
        sy, cy = math.sin(math.radians(self.yaw)), math.cos(math.radians(self.yaw))
        for batch in self.surface_batches:
            if self.materials[batch['material']]['rgba'][3] >= 1:
                opaque.append((batch, 0, len(batch['vertices'])//3))
            else:
                for i, (x,y,z) in enumerate(batch['centers']):
                    translucent.append((sp*(sy*x+cy*y)+cp*z, batch, i*3))
        draws = opaque + [(b, start, 3) for _,b,start in sorted(translucent, key=lambda x:x[0])]
        previous = None
        for batch, start, count in draws:
            if batch is previous:
                glDrawArrays(GL_TRIANGLES, start, count)
                continue
            previous = batch
            material = self.materials[batch['material']]
            rgba = material['rgba']
            texture = self.texture_ids.get(batch['material']) if self.show_textures else None
            glFrontFace(GL_CW if batch['mirrored'] else GL_CCW)
            glCullFace(GL_FRONT if batch['back'] else GL_BACK)
            glDepthMask(rgba[3] >= 1)
            glVertexPointer(3, GL_FLOAT, 0, batch['vertex_bytes'])
            if texture:
                glEnable(GL_TEXTURE_2D)
                glBindTexture(GL_TEXTURE_2D, texture)
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE)
                glColor4f(1, 1, 1, rgba[3])  # Colorized textures were processed in HLS on load.
                glEnableClientState(GL_TEXTURE_COORD_ARRAY)
                glTexCoordPointer(4, GL_FLOAT, 0, batch['uv_bytes'])
            else:
                glColor4f(*rgba)
                glDisable(GL_TEXTURE_2D)
                glDisableClientState(GL_TEXTURE_COORD_ARRAY)
            glDrawArrays(GL_TRIANGLES, start, count)
        glDepthMask(GL_TRUE)
        glFrontFace(GL_CCW)
        glDisableClientState(GL_TEXTURE_COORD_ARRAY)
        glDisableClientState(GL_VERTEX_ARRAY)
        glDisable(GL_TEXTURE_2D)
        glDisable(GL_CULL_FACE)
        glDisable(GL_BLEND)
        glDisable(GL_ALPHA_TEST)

    def camera_distance(self):
        aspect = max(self.width(), 1) / max(self.height(), 1)
        half_angle = math.atan(math.tan(math.radians(self.fov_y / 2)) * min(aspect, 1.0))
        # Vertices lie inside a unit sphere; fit it in the narrower field of view.
        return max(1.01, 1.15 * self.zoom / math.sin(half_angle))

    def paintGL(self):
        self.upload_textures()
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        aspect = max(self.width(), 1) / max(self.height(), 1)
        distance = self.camera_distance()
        near, far = max(0.001, distance - 1.05), distance + 1.05
        half_height = near * math.tan(math.radians(self.fov_y / 2))
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        glFrustum(-half_height * aspect, half_height * aspect, -half_height, half_height, near, far)
        glMatrixMode(GL_MODELVIEW)
        glLoadIdentity()
        glTranslatef(self.pan_x, self.pan_y, -distance)
        glRotatef(self.pitch, 1, 0, 0)
        glRotatef(self.yaw, 0, 0, 1)
        if self.triangles and (self.show_faces or self.show_triangulation):
            glEnableClientState(GL_VERTEX_ARRAY)
            glVertexPointer(3, GL_FLOAT, 0, self.triangles.tobytes())
            if self.show_faces:
                glColor3f(1.0, 1.0, 1.0)
                glEnable(GL_POLYGON_OFFSET_FILL)
                glPolygonOffset(1.0, 1.0)
                if self.surface_batches:
                    self.draw_surfaces()
                    glEnableClientState(GL_VERTEX_ARRAY)
                    glVertexPointer(3, GL_FLOAT, 0, self.triangles.tobytes())
                else:
                    glDrawArrays(GL_TRIANGLES, 0, len(self.triangles) // 3)
                glDisable(GL_POLYGON_OFFSET_FILL)
            if self.show_triangulation:
                glColor3f(0.0, 0.0, 0.0)
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)
                glLineWidth(1.0)
                glDrawArrays(GL_TRIANGLES, 0, len(self.triangles) // 3)
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL)
            glDisableClientState(GL_VERTEX_ARRAY)
        if self.vertices and self.show_edges:
            glColor3f(0.0, 0.0, 0.0)
            glLineWidth(1.0)
            glEnableClientState(GL_VERTEX_ARRAY)
            glVertexPointer(3, GL_FLOAT, 0, self.vertices.tobytes())
            glDrawArrays(GL_LINES, 0, len(self.vertices) // 3)
            glDisableClientState(GL_VERTEX_ARRAY)

    def mousePressEvent(self, event):
        self.last = event.pos()

    def mouseMoveEvent(self, event):
        if self.last is None:
            return
        delta = event.pos() - self.last
        self.last = event.pos()
        if event.buttons() & (Qt.MiddleButton | Qt.RightButton) or (
                event.buttons() & Qt.LeftButton and event.modifiers() & Qt.ShiftModifier):
            scale = (2 * self.camera_distance() * math.tan(math.radians(self.fov_y / 2))
                     / max(self.height(), 1))
            self.pan_x += delta.x() * scale
            self.pan_y -= delta.y() * scale
        elif event.buttons() & Qt.LeftButton:
            self.yaw += delta.x() * 0.5
            self.pitch += delta.y() * 0.5
        self.update()

    def wheelEvent(self, event):
        aspect = max(self.width(), 1) / max(self.height(), 1)
        half_angle = math.atan(math.tan(math.radians(self.fov_y / 2)) * min(aspect, 1.0))
        minimum_zoom = 1.01 * math.sin(half_angle) / 1.15
        self.zoom = max(minimum_zoom, min(1000, self.zoom * math.exp(-event.angleDelta().y() / 1200)))
        self.update()

    def mouseDoubleClickEvent(self, event):
        self.fit()

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_F:
            self.fit()
        else:
            super().keyPressEvent(event)


class Window(QMainWindow):
    def __init__(self, dll, screenshot=None):
        super().__init__()
        self.dll, self.screenshot, self.task = dll, screenshot, None
        self.setWindowTitle('SKP Viewer')
        self.resize(1100, 780)
        self.view = EdgeView()
        self.setCentralWidget(self.view)
        bar = self.addToolBar('Model')
        self.open_action = QAction('Open SKP…', self)
        self.open_action.setShortcut('Ctrl+O')
        self.open_action.triggered.connect(self.choose_file)
        bar.addAction(self.open_action)
        fit = QAction('Fit view (F)', self)
        fit.triggered.connect(self.view.fit)
        bar.addAction(fit)
        for label, attribute, checked in [('Faces', 'show_faces', True),
                                           ('Textures', 'show_textures', True),
                                           ('Edges', 'show_edges', True),
                                           ('Triangle edges', 'show_triangulation', False)]:
            action = QAction(label, self)
            action.setCheckable(True)
            action.setChecked(checked)
            def toggle(value, attr=attribute):
                setattr(self.view, attr, value)
                self.view.update()
            action.toggled.connect(toggle)
            bar.addAction(action)
        self.statusBar().showMessage('Left drag: rotate | Right/middle drag: pan | Wheel: zoom | F: fit')

    def choose_file(self):
        path, _ = QFileDialog.getOpenFileName(self, 'Open SketchUp model', str(ROOT), 'SketchUp (*.skp)')
        if path:
            self.load(path)

    def load(self, path):
        if self.task is not None:
            return
        self.open_action.setEnabled(False)
        self.statusBar().showMessage('Reading ' + str(path))
        self.task = LoadTask(self.dll, path)
        self.task.loaded.connect(lambda edges: self.on_loaded(path, edges))
        self.task.failed.connect(self.on_error)
        self.task.finished.connect(self.on_finished)
        self.task.start()

    def on_finished(self):
        self.task.deleteLater()
        self.task = None
        self.open_action.setEnabled(True)

    def on_loaded(self, path, geometry):
        edges, triangles, surface = geometry
        self.view.set_edges(edges, triangles, surface)
        self.setWindowTitle(Path(path).name + ' — SKP Viewer')
        self.statusBar().showMessage('%d edges | %d triangles | Left: rotate | Right/middle: pan | Wheel: zoom | F: fit' % (len(edges), len(triangles)))
        if surface.get('uv_fallbacks'):
            self.statusBar().showMessage(self.statusBar().currentMessage() +
                ' | %d invalid UV sides: default mapping used' % surface['uv_fallbacks'])
        print('Loaded %d edges and %d triangles from %s' % (len(edges), len(triangles), path), flush=True)
        if self.screenshot:
            QTimer.singleShot(1000, self.capture)

    def capture(self):
        picture = self.view.grabFramebuffer()
        if not self.view.isValid() or picture.isNull() or not picture.save(self.screenshot):
            print('OpenGL capture failed', file=sys.stderr)
            QApplication.exit(1)
        else:
            print('OpenGL screenshot: ' + self.screenshot, flush=True)
            QApplication.quit()

    def on_error(self, message):
        self.statusBar().showMessage('Load failed')
        if self.screenshot:
            print(message, file=sys.stderr)
            QTimer.singleShot(100, lambda: QApplication.exit(1))
        else:
            QMessageBox.critical(self, 'Cannot load model', message)

    def closeEvent(self, event):
        if self.task is not None and self.task.isRunning():
            event.ignore()
            self.statusBar().showMessage('Please wait for the model reader to finish before closing.')
        else:
            event.accept()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model', nargs='?', default=str(ROOT / 'test2_box.skp'))
    parser.add_argument('--dll', default=str(ROOT / 'build_x64/Release/slapi.dll'))
    parser.add_argument('--screenshot', help='Save OpenGL framebuffer and exit (smoke test)')
    args = parser.parse_args()
    fmt = QSurfaceFormat()
    fmt.setVersion(2, 1)
    fmt.setProfile(QSurfaceFormat.CompatibilityProfile)
    fmt.setDepthBufferSize(24)
    QSurfaceFormat.setDefaultFormat(fmt)
    app = QApplication(sys.argv[:1])
    window = Window(args.dll, args.screenshot)
    window.show()
    QTimer.singleShot(0, lambda: window.load(args.model))
    if args.screenshot:
        QTimer.singleShot(30000, lambda: app.exit(2))
    return app.exec_()


if __name__ == '__main__':
    sys.exit(main())
