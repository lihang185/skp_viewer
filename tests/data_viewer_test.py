"""Load every data model through the same x64 ctypes path as the viewer."""
import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'viewer'))
from skp_edges import EdgeReader


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--one', type=Path)
    parser.add_argument('--faces', action='store_true')
    parser.add_argument('--textures', action='store_true')
    args = parser.parse_args()
    if args.one:
        result = EdgeReader(ROOT / 'build_x64/Release/slapi.dll').load(
            args.one, with_faces=args.faces, with_textures=args.textures)
        surface = result[2] if args.textures else None
        segments, triangles = result[:2] if args.faces or args.textures else (result, [])
        assert all(math.isfinite(v) for edge in segments for point in edge for v in point)
        assert all(math.isfinite(v) for tri in triangles for point in tri for v in point)
        row = {'file': args.one.name, 'ok': True, 'displayed_edges': len(segments),
               'displayed_triangles': len(triangles)}
        if surface:
            from skp_textures import decode_material_images
            decode_material_images(surface)
            assert all(math.isfinite(v) for b in surface['batches'] for uv in b['uvq'] for v in uv)
            assert all(abs(uv[3]) > 1e-15 for b in surface['batches'] for uv in b['uvq'])
            row['decoded_textures'] = sum('pixels' in m for m in surface['materials'].values())
            row['uv_fallback_sides'] = surface['uv_fallbacks']
            row['textured_triangle_sides'] = sum(len(b['points'])//3 for b in surface['batches']
                                                if surface['materials'][b['material']]['image'])
        print(json.dumps(row))
        return
    results = []
    for path in sorted((ROOT / 'data').rglob('*.skp')):
        try:
            run = subprocess.run([sys.executable, __file__, '--one', str(path)] +
                                 (['--faces'] if args.faces else []) + (['--textures'] if args.textures else []),
                                 capture_output=True, text=True, encoding='utf-8', timeout=120)
            row = json.loads(run.stdout) if run.returncode == 0 else {
                'file': path.name, 'ok': False, 'error': run.stderr.strip(), 'exit_code': run.returncode}
        except Exception as exc:
            row = {'file': path.name, 'ok': False, 'error': str(exc)}
        results.append(row)
        print(json.dumps(row, ensure_ascii=True), flush=True)
    report = ('analysis/data_texture_viewer_results.json' if args.textures else
              'analysis/data_mesh_viewer_results.json' if args.faces else 'analysis/data_viewer_results.json')
    (ROOT / report).write_text(
        json.dumps(results, indent=2, ensure_ascii=False), encoding='utf-8')
    sys.exit(0 if results and all(r['ok'] for r in results) else 1)


if __name__ == '__main__':
    main()
