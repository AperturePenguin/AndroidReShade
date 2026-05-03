"""生成示例 LUT 文件（暖调/冷调/胶片感）"""

def write_cube(filename, title, transform_fn, size=8):
    lines = [
        f'TITLE "{title}"',
        'DOMAIN_MIN 0.0 0.0 0.0',
        'DOMAIN_MAX 1.0 1.0 1.0',
        f'LUT_3D_SIZE {size}',
        ''
    ]
    for b in range(size):
        for g in range(size):
            for r in range(size):
                ri = r / (size - 1)
                gi = g / (size - 1)
                bi = b / (size - 1)
                ro, go, bo = transform_fn(ri, gi, bi)
                ro = max(0.0, min(1.0, ro))
                go = max(0.0, min(1.0, go))
                bo = max(0.0, min(1.0, bo))
                lines.append(f'{ro:.6f} {go:.6f} {bo:.6f}')
    with open(filename, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'Written: {filename}')

# Warm: 暖橙/黄调
def warm(r, g, b):
    r2 = r * 1.08 + 0.03
    g2 = g * 1.02 + 0.01
    b2 = b * 0.88 - 0.02
    return r2, g2, b2

# Cool: 冷蓝调
def cool(r, g, b):
    r2 = r * 0.90 - 0.02
    g2 = g * 0.97
    b2 = b * 1.10 + 0.04
    return r2, g2, b2

# Film: S曲线对比度 + 暗部提升（胶片感）
def film(r, g, b):
    def scurve(x):
        x = x * 0.94 + 0.03  # lift blacks
        x = x * x * (3 - 2*x)  # smoothstep S-curve
        return x
    r2 = scurve(r) * 1.05
    g2 = scurve(g) * 1.02
    b2 = scurve(b) * 0.98 + 0.01
    return r2, g2, b2

# Teal & Orange: 电影感青橙调
def teal_orange(r, g, b):
    # 高光偏橙，暗部偏青
    luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
    # 暗部 -> 青
    shadow_teal = (r * 0.85, g * 1.02, b * 1.12)
    # 高光 -> 橙
    highlight_orange = (r * 1.08 + 0.04, g * 1.02 - 0.01, b * 0.82 - 0.03)
    t = luma  # blend factor
    r2 = shadow_teal[0] * (1 - t) + highlight_orange[0] * t
    g2 = shadow_teal[1] * (1 - t) + highlight_orange[1] * t
    b2 = shadow_teal[2] * (1 - t) + highlight_orange[2] * t
    return r2, g2, b2

base = 'C:/Users/Hao/WorkBuddy/Claw/AndroidReShade/assets/luts/'
write_cube(base + 'warm_tone.cube',    'Warm Tone - Orange/Yellow Tint',   warm)
write_cube(base + 'cool_tone.cube',    'Cool Tone - Blue/Cyan Tint',        cool)
write_cube(base + 'film_look.cube',    'Film Look - S-Curve Contrast',      film)
write_cube(base + 'teal_orange.cube',  'Teal & Orange - Cinematic',         teal_orange)
print('All LUTs generated!')
