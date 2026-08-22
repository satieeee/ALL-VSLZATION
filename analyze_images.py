"""Comprehensive geometric analysis of hard-surface reference images."""
from PIL import Image
import os, sys, math

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

def analyze_image(path, name):
    img = Image.open(path).convert('RGB')
    w, h = img.size
    gray = img.convert('L')
    pixels = gray.load()

    result = {}
    result['name'] = name
    result['size'] = (w, h)
    result['aspect'] = w / h

    # --- 1. Foreground/Background separation ---
    # Simple threshold: bright pixels tend to be object, dark = background
    # We'll use Otsu-like approach: analyze histogram
    hist = [0] * 256
    for y in range(h):
        for x in range(w):
            hist[pixels[x, y]] += 1

    total = w * h
    # Find dominant background color (usually most common dark or light value)
    sorted_vals = sorted(range(256), key=lambda v: hist[v], reverse=True)
    bg_val = sorted_vals[0]  # Most common value
    bg_count = hist[bg_val] / total
    result['bg_dominance'] = bg_count
    result['bg_brightness'] = bg_val

    # Estimate foreground bounding box (non-background pixels)
    threshold = 40  # brightness difference threshold
    min_x, max_x = w, 0
    min_y, max_y = h, 0
    fg_count = 0
    for y in range(h):
        for x in range(w):
            if abs(pixels[x, y] - bg_val) > threshold:
                fg_count += 1
                min_x = min(min_x, x)
                max_x = max(max_x, x)
                min_y = min(min_y, y)
                max_y = max(max_y, y)

    if fg_count > 0:
        fg_w = max_x - min_x
        fg_h = max_y - min_y
        result['fg_bbox'] = (min_x, min_y, max_x, max_y)
        result['fg_size'] = (fg_w, fg_h)
        result['fg_ratio'] = fg_w / fg_h if fg_h > 0 else 0
        result['fg_center'] = ((min_x + max_x) / 2 / w, (min_y + max_y) / 2 / h)
        result['fg_coverage'] = fg_count / total
    else:
        result['fg_bbox'] = (0, 0, w-1, h-1)
        result['fg_size'] = (w, h)
        result['fg_ratio'] = w / h
        result['fg_center'] = (0.5, 0.5)
        result['fg_coverage'] = 1.0

    # --- 2. Vertical symmetry analysis ---
    # Check multiple horizontal strips
    center_x = w // 2
    strip_count = 7
    sym_scores = []
    for i in range(strip_count):
        y = int(h * (i + 1) / (strip_count + 1))
        diffs = []
        for x in range(center_x):
            l = pixels[x, y]
            r = pixels[w - 1 - x, y]
            diffs.append(abs(l - r))
        if diffs:
            score = 1.0 - (sum(diffs) / (len(diffs) * 255))
            sym_scores.append(score)
    result['h_symmetry'] = sum(sym_scores) / len(sym_scores) if sym_scores else 0
    result['h_symmetry_strips'] = sym_scores

    # --- 3. Horizontal symmetry (top/bottom) ---
    center_y = h // 2
    hsym_scores = []
    for i in range(strip_count):
        x = int(w * (i + 1) / (strip_count + 1))
        diffs = []
        for y in range(center_y):
            t = pixels[x, y]
            b = pixels[x, h - 1 - y]
            diffs.append(abs(t - b))
        if diffs:
            score = 1.0 - (sum(diffs) / (len(diffs) * 255))
            hsym_scores.append(score)
    result['v_symmetry'] = sum(hsym_scores) / len(hsym_scores) if hsym_scores else 0

    # --- 4. Edge density (simple gradient) ---
    # Sobel-like edge detection on downsampled grid
    step = 4
    edge_count = 0
    sample_count = 0
    for y in range(step, h - step, step):
        for x in range(step, w - step, step):
            gx = pixels[x+1, y] - pixels[x-1, y]
            gy = pixels[x, y+1] - pixels[x, y-1]
            mag = math.sqrt(gx*gx + gy*gy)
            if mag > 30:
                edge_count += 1
            sample_count += 1

    result['edge_density'] = edge_count / sample_count if sample_count > 0 else 0

    # --- 5. Regional brightness analysis ---
    # Divide into 5x5 grid
    grid = 5
    region_brightness = []
    for gy in range(grid):
        row = []
        for gx in range(grid):
            x0 = w * gx // grid
            x1 = w * (gx + 1) // grid
            y0 = h * gy // grid
            y1 = h * (gy + 1) // grid
            region_pixels = []
            for y in range(y0, y1, 3):
                for x in range(x0, x1, 3):
                    region_pixels.append(pixels[x, y])
            avg = sum(region_pixels) / len(region_pixels)
            row.append(round(avg))
        region_brightness.append(row)
    result['region_brightness'] = region_brightness

    # --- 6. Contour detection ---
    # Find horizontal and vertical profile lines
    h_profile = []
    for y in range(0, h, 2):
        transitions = 0
        prev_dark = pixels[0, y] < 128
        for x in range(1, w, 2):
            curr_dark = pixels[x, y] < 128
            if curr_dark != prev_dark:
                transitions += 1
            prev_dark = curr_dark
        h_profile.append(transitions)

    v_profile = []
    for x in range(0, w, 2):
        transitions = 0
        prev_dark = pixels[x, 0] < 128
        for y in range(1, h, 2):
            curr_dark = pixels[x, y] < 128
            if curr_dark != prev_dark:
                transitions += 1
            prev_dark = curr_dark
        v_profile.append(transitions)

    result['h_profile_avg_transitions'] = sum(h_profile) / len(h_profile) if h_profile else 0
    result['v_profile_avg_transitions'] = sum(v_profile) / len(v_profile) if v_profile else 0
    result['total_detail'] = (result['h_profile_avg_transitions'] + result['v_profile_avg_transitions']) / 2

    # --- 7. Corner/feature point detection ---
    # Harris-like: for each sample point, check gradient covariance
    corner_count = 0
    for y in range(4, h - 4, 6):
        for x in range(4, w - 4, 6):
            ix = pixels[x+1, y] - pixels[x-1, y]
            iy = pixels[x, y+1] - pixels[x, y-1]
            ixx = ix * ix
            iyy = iy * iy
            ixy = ix * iy
            # Simple cornerness measure
            det = ixx * iyy - ixy * ixy
            trace = ixx + iyy
            if trace > 0 and det / (trace * trace + 1) > 0.01:
                corner_count += 1
    result['corner_density'] = corner_count / ((w//6) * (h//6)) if w > 0 and h > 0 else 0

    # --- 8. Circular/radial feature detection ---
    # Check center region for radial symmetry
    cx, cy = w // 2, h // 2
    max_radius = min(w, h) // 3
    radial_profile = []
    for r in range(10, max_radius, 10):
        samples = []
        for angle in range(0, 360, 15):
            rad = math.radians(angle)
            sx = int(cx + r * math.cos(rad))
            sy = int(cy + r * math.sin(rad))
            if 0 <= sx < w and 0 <= sy < h:
                samples.append(pixels[sx, sy])
        if samples:
            avg = sum(samples) / len(samples)
            variance = sum((s - avg) ** 2 for s in samples) / len(samples)
            radial_profile.append(variance)
    result['radial_variance'] = sum(radial_profile) / len(radial_profile) if radial_profile else 0
    result['radial_consistency'] = 1.0 / (1.0 + result['radial_variance'] / 1000)

    return result


def print_analysis(r):
    """Print structured analysis."""
    name = r['name']
    w, h = r['size']

    print(f"\n{'='*80}")
    print(f"IMAGE: {name}")
    print(f"{'='*80}")
    print(f"  Resolution: {w}x{h}  Aspect: {r['aspect']:.3f}  ({'portrait' if r['aspect'] < 1 else 'landscape' if r['aspect'] > 1 else 'square'})")
    print(f"  Background: brightness={r['bg_brightness']} dominance={r['bg_dominance']:.2%}")
    print(f"  Foreground coverage: {r['fg_coverage']:.1%}  FG aspect: {r['fg_ratio']:.3f}")
    print(f"  FG center: ({r['fg_center'][0]:.2%}, {r['fg_center'][1]:.2%})")
    print(f"  H-symmetry (bilateral): {r['h_symmetry']:.3f}  V-symmetry (top-bottom): {r['v_symmetry']:.3f}")
    print(f"  H-symmetry strips: {[f'{s:.2f}' for s in r['h_symmetry_strips']]}")
    print(f"  Edge density: {r['edge_density']:.3f}")
    print(f"  Total detail (profile transitions): {r['total_detail']:.1f}")
    print(f"  Corner density: {r['corner_density']:.4f}")
    print(f"  Radial consistency: {r['radial_consistency']:.3f}")

    # Region brightness grid
    print(f"  5x5 Brightness grid (0=dark, 255=bright):")
    for row in r['region_brightness']:
        bars = ''.join('█' if v < 50 else '▓' if v < 100 else '▒' if v < 150 else '░' if v < 200 else ' ' for v in row)
        print(f"    [{','.join(f'{v:3d}' for v in row)}]  {bars}")

    # Interpretations
    print(f"\n  --- INTERPRETATION ---")

    # Symmetry type
    if r['h_symmetry'] > 0.85:
        print(f"  Symmetry: STRONG bilateral (mirror) symmetry detected")
    elif r['h_symmetry'] > 0.7:
        print(f"  Symmetry: MODERATE bilateral symmetry (possibly with asymmetric details)")
    elif r['h_symmetry'] > 0.5:
        print(f"  Symmetry: WEAK bilateral symmetry")
    else:
        print(f"  Symmetry: ASYMMETRIC or radial composition")

    if r['radial_consistency'] > 0.8:
        print(f"  Radial: High radial consistency - likely circular/cylindrical dominant forms")
    elif r['radial_consistency'] > 0.5:
        print(f"  Radial: Moderate radial features detected")

    # Detail level
    if r['edge_density'] > 0.3:
        print(f"  Detail: HIGH detail density (many surface features, panel lines, greebles)")
    elif r['edge_density'] > 0.15:
        print(f"  Detail: MEDIUM detail density (moderate surface articulation)")
    else:
        print(f"  Detail: LOW detail density (smooth, large-form dominant)")

    if r['corner_density'] > 0.02:
        print(f"  Features: Many corner/feature points - complex surface topology")
    elif r['corner_density'] > 0.01:
        print(f"  Features: Moderate feature points")
    else:
        print(f"  Features: Few distinct features - primarily smooth surfaces")

    # Aspect-based primitive guess
    fg_ar = r['fg_ratio']
    fg_center = r['fg_center']
    if abs(fg_center[0] - 0.5) < 0.1 and abs(fg_center[1] - 0.5) < 0.1:
        print(f"  Composition: CENTERED subject")
    elif fg_center[0] < 0.4:
        print(f"  Composition: LEFT-weighted")
    elif fg_center[0] > 0.6:
        print(f"  Composition: RIGHT-weighted")

    # Estimate dominant primitives based on edges, symmetry, and profiles
    if r['h_symmetry'] > 0.8 and r['edge_density'] < 0.2 and r['radial_consistency'] < 0.5:
        print(f"  Primitives guess: BOX-like forms dominant (high symmetry, low edges, low radial)")

    return r


# Run analysis
results = []
for fname in FILES:
    path = os.path.join(BASE, fname)
    try:
        r = analyze_image(path, fname)
        results.append(r)
        print_analysis(r)
    except Exception as e:
        print(f"\nERROR analyzing {fname}: {e}")
        import traceback
        traceback.print_exc()

print(f"\n{'='*80}")
print(f"SUMMARY: Analyzed {len(results)}/{len(FILES)} images successfully")
print(f"{'='*80}")
