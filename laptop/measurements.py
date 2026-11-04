"""Body-axis rise/run grades; ADA references are comparison aids, not certification."""
import math

ADA_WALKING_RUNNING = 100 / 20
ADA_RAMP_RUNNING = 100 / 12
ADA_CROSS = 100 / 48
ADA_SOURCE = 'https://www.access-board.gov/ada/chapter/ch04/'


def grades(q, forward='X'):
    if len(q) != 4 or any(v is None or not math.isfinite(v) for v in q):
        return dict(grade_x=None, grade_y=None, running=None, cross=None)
    norm = math.sqrt(sum(v*v for v in q))
    if norm < .5:
        return dict(grade_x=None, grade_y=None, running=None, cross=None)
    w, x, y, z = [v/norm for v in q]
    # Body-to-world rotation columns; sensor XY plane is the surface plane.
    vx = [1-2*(y*y+z*z), 2*(x*y+w*z), 2*(x*z-w*y)]
    vy = [2*(x*y-w*z), 1-2*(x*x+z*z), 2*(y*z+w*x)]
    n = [2*(x*z+w*y), 2*(y*z-w*x), 1-2*(x*x+y*y)]
    def grade(v):
        run = math.hypot(v[0], v[1])
        return 100*v[2]/run if run > 1e-6 else None
    f = vx if forward == 'X' else vy
    run = math.hypot(f[0], f[1])
    # True cross direction is horizontally perpendicular to running direction;
    # projected sensor X/Y directions need not be perpendicular on a tilted plane.
    cross = None
    if run > 1e-6 and abs(n[2]) > 1e-6:
        cx, cy = -f[1]/run, f[0]/run
        cross = -100*(n[0]*cx+n[1]*cy)/n[2]
    return dict(grade_x=grade(vx), grade_y=grade(vy), running=grade(f), cross=cross)


def comparison(running, cross, surface='walking'):
    limit = ADA_RAMP_RUNNING if surface == 'ramp' else ADA_WALKING_RUNNING
    if running is None or cross is None:
        return 'undefined'
    return 'above reference' if abs(running)>limit or abs(cross)>ADA_CROSS else 'within reference'


def calibration_ready(values, fusion='NDOF'):
    if len(values) != 4:
        return False
    if fusion == 'IMUPLUS':
        return values[1] == 3 and values[2] == 3
    return fusion == 'NDOF' and values == [3, 3, 3, 3]
