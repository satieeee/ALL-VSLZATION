"""Enhanced geometric analysis with ASCII art renderings for 3D modeling reference."""
from PIL import Image
import os, math, json

BASE = r'D:\C++ prog\ALL VSLZATION\effect reference\mtht'
FILES = [
    'bbe0d3115c2dc45db8127b50.jpg',
    'c5b28fc207a3b8373c58c874a0055d53.jpg',
    'd2f839b9276d319340bf5c02568a7e72.jpg',
    'd610081bbb833c601a082bf4143cf749630fc7ff.jpg',
    'e46476a31df939d3e408b205c5694a2c.jpg',
    'file.jpg',
    'images.jpg',
    'mth1.PNG',
    'mth2.PNG',
    'mth3.PNG',
    'u=1380501058,1815705789&fm=253&fmt=auto&app=138&f=JPEG.png',
]

CHARS = ' .:-=+*#%@'

def analyze_full(path, name):
    img = Image.open(path).convert('RGB')
    w, h = img.size
    gray = img.convert('L')
    pixels = gray.load()

    r = {'name': name, 'w': w, 'h': h, 'aspect': w/h}

    # --- ASCII art rendering (80 cols wide) ---
    ascii_w = 80
    ascii_h = int(h * ascii_w / w)
    ascii_h = max(20, min(60, ascii_h))
    ascii_w = int(w * ascii_h / h)
    ascii_lines = []
    for ay in range(ascii_h):
        line = ''
        for ax in range(ascii_w):
            px = int(ax * w / ascii_w)
            py = int(ay * h / ascii_h)
            brightness = pixels[px, py]
            char_idx = int(brightness / 256 * len(CHARS))
            char_idx = max(0, min(len(CHARS)-1, char_idx))
            line += CHARS[char_idx]
        ascii_lines.append(line)
    r['ascii'] = ascii_lines

    # --- Symmetry analysis ---
    cw = w // 2
    # Bilateral symmetry at multiple rows
    sym_rows = []
    for frac in [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9]:
        y = int(h * frac)
        diff_sum = 0
        for x in range(cw):
            diff_sum += abs(pixels[x, y] - pixels[w-1-x, y])
        sym_rows.append(1.0 - diff_sum / (cw * 255))
    r['h_sym_mean'] = sum(sym_rows) / len(sym_rows)
    r['h_sym_min'] = min(sym_rows)
    r['h_sym_max'] = max(sym_rows)
    r['h_sym_rows'] = [round(s, 3) for s in sym_rows]

    # --- Edge detection + orientation analysis ---
    step = 3
    edge_data = []  # (x, y, mag, orientation)
    for y in range(step, h - step, step):
        for x in range(step, w - step, step):
            gx = pixels[x+step, y] - pixels[x-step, y]
            gy = pixels[x, y+step] - pixels[x, y-step]
            mag = math.sqrt(gx*gx + gy*gy)
            if mag > 20:
                angle = math.atan2(gy, gx)
                edge_data.append((x, y, mag, angle))
    r['edge_count'] = len(edge_data)
    r['edge_density_pct'] = round(len(edge_data) / ((w//step) * (h//step)) * 100, 1) if w > 0 else 0

    # Analyze edge orientation distribution
    if edge_data:
        angle_bins = [0, 0, 0, 0]  # horizontal, vertical, diag1, diag2
        for _, _, _, ang in edge_data:
            a = abs(ang) % math.pi
            if a < 0.2 or a > math.pi - 0.2:
                angle_bins[0] += 1  # horizontal
            elif abs(a - math.pi/2) < 0.2:
                angle_bins[1] += 1  # vertical
            elif abs(a - math.pi/4) < 0.2:
                angle_bins[2] += 1  # diag /
            elif abs(a - 3*math.pi/4) < 0.2:
                angle_bins[3] += 1  # diag \
        total_e = len(edge_data)
        r['edge_orientations'] = {
            'horizontal_pct': round(angle_bins[0]/total_e*100, 1),
            'vertical_pct': round(angle_bins[1]/total_e*100, 1),
            'diagonal_pct': round((angle_bins[2]+angle_bins[3])/total_e*100, 1),
        }
        # Dominant edge direction
        dom = max(range(4), key=lambda i: angle_bins[i])
        r['edge_dominant'] = ['horizontal', 'vertical', 'diagonal(/)', 'diagonal(\\)'][dom]

    # --- Region segmentation ---
    # Find contiguous bright/dark regions using flood-fill sampling
    # Sample at grid points
    grid_res = 20
    grid_w = w // grid_res
    grid_h = h // grid_res
    visited = [[False]*grid_w for _ in range(grid_h)]
    regions = []
    for gy in range(grid_h):
        for gx in range(grid_w):
            if visited[gy][gx]:
                continue
            px = gx * grid_res + grid_res // 2
            py = gy * grid_res + grid_res // 2
            b = pixels[px, py]
            # Simple flood fill
            stack = [(gx, gy)]
            region_cells = []
            while stack:
                cx, cy = stack.pop()
                if cx < 0 or cx >= grid_w or cy < 0 or cy >= grid_h:
                    continue
                if visited[cy][cx]:
                    continue
                cpx = cx * grid_res + grid_res // 2
                cpy = cy * grid_res + grid_res // 2
                cb = pixels[cpx, cpy]
                if abs(cb - b) > 30:
                    continue
                visited[cy][cx] = True
                region_cells.append((cx, cy))
                for dx, dy in [(-1,0),(1,0),(0,-1),(0,1)]:
                    stack.append((cx+dx, cy+dy))
            if len(region_cells) >= 3:
                avg_b = sum(pixels[cx*grid_res+grid_res//2, cy*grid_res+grid_res//2] for cx, cy in region_cells) / len(region_cells)
                min_x = min(c for c,_ in region_cells)
                max_x = max(c for c,_ in region_cells)
                min_y = min(r for _,r in region_cells)
                max_y = max(r for _,r in region_cells)
                regions.append({
                    'size_cells': len(region_cells),
                    'avg_brightness': round(avg_b),
                    'bbox': (min_x, min_y, max_x, max_y),
                    'center': ((min_x+max_x)/2/grid_w, (min_y+max_y)/2/grid_h),
                    'aspect': (max_x-min_x+1)/(max_y-min_y+1) if max_y > min_y else 1,
                })

    # Sort regions by size, keep top 8
    regions.sort(key=lambda x: -x['size_cells'])
    r['regions'] = regions[:8]

    # --- Edge map as ASCII ---
    edge_map_w = 80
    edge_map_h = int(h * edge_map_w / w)
    edge_map_h = max(20, min(60, edge_map_h))
    edge_map_w = int(w * edge_map_h / h)
    edge_lines = []
    for ay in range(edge_map_h):
        line = ''
        for ax in range(edge_map_w):
            px = int(ax * w / edge_map_w)
            py = int(ay * h / edge_map_h)
            if px > 0 and px < w-1 and py > 0 and py < h-1:
                gx = pixels[px+1, py] - pixels[px-1, py]
                gy = pixels[px, py+1] - pixels[px, py-1]
                mag = math.sqrt(gx*gx + gy*gy)
                if mag > 40:
                    line += '#'
                elif mag > 20:
                    line += '+'
                elif mag > 10:
                    line += '.'
                else:
                    line += ' '
            else:
                line += ' '
        edge_lines.append(line)
    r['edge_map'] = edge_lines

    # --- Brightness histogram analysis ---
    hist = [0]*256
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            hist[pixels[x, y]] += 1
    # Find peaks
    peaks = []
    for i in range(1, 255):
        if hist[i] > hist[i-1] and hist[i] > hist[i+1] and hist[i] > max(hist)*0.01:
            peaks.append((i, hist[i]))
    peaks.sort(key=lambda x: -x[1])
    r['hist_peaks'] = peaks[:5]

    return r


def format_analysis(r):
    lines = []
    lines.append(f"\n{'='*80}")
    lines.append(f"IMAGE {FILES.index(r['name'])+1}: {r['name']}")
    lines.append(f"{'='*80}")
    lines.append(f"Resolution: {r['w']}x{r['h']}  Aspect: {r['aspect']:.3f}")

    lines.append(f"\n--- ASCII RENDERING (80 cols) ---")
    for al in r['ascii']:
        lines.append(al)

    lines.append(f"\n--- EDGE MAP ---")
    for el in r['edge_map']:
        lines.append(el)

    lines.append(f"\n--- SYMMETRY ---")
    lines.append(f"Bilateral H-symmetry: mean={r['h_sym_mean']:.3f} min={r['h_sym_min']:.3f} max={r['h_sym_max']:.3f}")
    lines.append(f"By row (top to bottom): {r['h_sym_rows']}")

    lines.append(f"\n--- EDGE ANALYSIS ---")
    lines.append(f"Edge points detected: {r['edge_count']} (density: {r['edge_density_pct']}%)")
    if 'edge_orientations' in r:
        eo = r['edge_orientations']
        lines.append(f"Orientation: Horizontal={eo['horizontal_pct']}% Vertical={eo['vertical_pct']}% Diagonal={eo['diagonal_pct']}%")
        lines.append(f"Dominant direction: {r['edge_dominant']}")

    lines.append(f"\n--- BRIGHTNESS PEAKS ---")
    for val, count in r['hist_peaks']:
        lines.append(f"  Peak at brightness {val}: {count} pixels ({(count/(r['w']*r['h']/4))*100:.1f}%)")

    lines.append(f"\n--- SEGMENTED REGIONS (top {len(r['regions'])}) ---")
    for i, reg in enumerate(r['regions']):
        lines.append(f"  Region {i+1}: {reg['size_cells']} cells, brightness={reg['avg_brightness']}, "
                     f"center=({reg['center'][0]:.2f},{reg['center'][1]:.2f}), aspect={reg['aspect']:.2f}")

    return '\n'.join(lines)


# Main
for fname in FILES:
    path = os.path.join(BASE, fname)
    try:
        r = analyze_full(path, fname)
        output = format_analysis(r)
        # Write to file instead of print (to avoid encoding issues)
        out_path = os.path.join(BASE, f'analysis_{FILES.index(fname)+1:02d}.txt')
        with open(out_path, 'w', encoding='utf-8') as f:
            f.write(output)
        print(f"OK: {fname} -> analysis_{FILES.index(fname)+1:02d}.txt")
    except Exception as e:
        print(f"ERR: {fname}: {e}")
        import traceback
        traceback.print_exc()

print("\nDone. Analysis files written.")
