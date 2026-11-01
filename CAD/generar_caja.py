import cadquery as cq

# ---------------- Parametros (marco de coordenadas = el del STEP de tu PCB) ----------------
# PCB medida del STEP
BX0, BX1, BY0, BY1 = 97.5, 203.0, -125.5, -72.5
PCB_T = 1.51
HOLES = [(102.5,-119.5),(102.5,-78.0),(197.5,-119.5),(197.5,-78.0)]   # M3, d=3.2
SW_RAISE = 5.8             # los switches del STEP estan hundidos: hay que subirlos esta cantidad
SW_TOP = 6.695 + SW_RAISE  # tope de la carcasa del switch (12.5)
CLR = 0.5                  # holgura PCB-cavidad
WALL = 8.0                 # pared
# cavidad
CX0, CX1, CY0, CY1 = BX0-CLR, BX1+CLR, BY0-CLR, BY1+CLR
# pantalla (de su STEP): 62 x 29 x 4.7, vidrio 49.72 x 25.8 en la cara frontal
D_L, D_H, D_T = 62.0, 29.0, 4.7
GLASS_W, GLASS_H = 49.72, 25.8
TOL = 0.5
PCB_CX = (BX0+BX1)/2.0    # 150.25
FRONT_WALL = 3.0           # 3 mm delante de la pantalla
BAY_W = D_T + 2*TOL        # 5.7
BAY_Y0, BAY_Y1 = CY0-BAY_W, CY0
BAY_X0, BAY_X1 = PCB_CX-D_L/2-TOL, PCB_CX+D_L/2+TOL
# envolvente exterior
OX0, OX1 = CX0-WALL, CX1+WALL
OY1 = CY1+WALL
OY0 = BAY_Y0-FRONT_WALL
# alturas
PLATE_T = 3.0
PLATE_TOP = SW_TOP + 1.0        # placa ~ a ras del cuerpo del switch (+1 mm)
Z_WALL_TOP = PLATE_TOP - PLATE_T
WALL_H = 33.0                   # igual que la guia
Z_FLOOR = Z_WALL_TOP - WALL_H   # piso interior
BASE_T = 3.0
Z_BASE_BOT = Z_FLOOR - BASE_T
GROOVE = 1.5                    # ranura de la pantalla en el piso
# soportes PCB
INSERT_D, INSERT_DEPTH = 4.6, 6.0
POST_HALF = 4.0
# pantalla colocada
D_BOTTOM = Z_FLOOR - GROOVE
D_ZC = D_BOTTOM + D_H/2
D_YGLASS = BAY_Y0 + TOL          # cara del vidrio
D_YBACK = D_YGLASS + D_T

def box(x0,x1,y0,y1,z0,z1):
    return (cq.Workplane("XY").box(x1-x0,y1-y0,z1-z0,centered=False).translate((x0,y0,z0)))

# ---------------- BASE ----------------
outer = box(OX0,OX1,OY0,OY1,Z_BASE_BOT,Z_WALL_TOP).edges("|Z").fillet(3.0)
cavity = box(CX0,CX1,CY0,CY1,Z_FLOOR,Z_WALL_TOP+1)
bay    = box(BAY_X0,BAY_X1,BAY_Y0,BAY_Y1,D_BOTTOM,Z_WALL_TOP+1)
base = outer.cut(cavity).cut(bay)

# soportes de la PCB (rectangulos que llegan a la pared) hasta la cara inferior de la PCB (z=0)
for (hx,hy) in HOLES:
    x0 = CX0 if hx < PCB_CX else hx-POST_HALF
    x1 = hx+POST_HALF if hx < PCB_CX else CX1
    y0 = CY0 if hy < (BY0+BY1)/2 else hy-POST_HALF
    y1 = hy+POST_HALF if hy < (BY0+BY1)/2 else CY1
    base = base.union(box(x0,x1,y0,y1,Z_FLOOR-0.5,0.0))
for (hx,hy) in HOLES:   # agujero para inserto roscado M3
    base = base.cut(cq.Workplane("XY").circle(INSERT_D/2).extrude(INSERT_DEPTH).translate((hx,hy,-INSERT_DEPTH)))

# agujeros para insertos de la tapa (en la parte alta de las paredes)
xl, xr = (OX0+CX0)/2, (OX1+CX1)/2
yb, yf = (OY1+CY1)/2, (OY0+BAY_Y0)/2 - 0.0
yf = (OY0 + CY0)/2
SCREWS = [(xl,yb),(xr,yb),(xl,yf),(xr,yf)]
for (sx,sy) in SCREWS:
    base = base.cut(cq.Workplane("XY").circle(INSERT_D/2).extrude(INSERT_DEPTH+0.01).translate((sx,sy,Z_WALL_TOP-INSERT_DEPTH)))

# ventana de la pantalla (pared frontal)
WIN_W, WIN_H = GLASS_W-2.0, GLASS_H-2.0
base = base.cut(box(PCB_CX-WIN_W/2,PCB_CX+WIN_W/2,OY0-1,BAY_Y0+0.01,D_ZC-WIN_H/2,D_ZC+WIN_H/2))
# USB-C (pared trasera): conector en x 146.51..155.45, z 1.61..5.81
usb_cx = (146.51+155.45)/2
base = base.cut(box(usb_cx-6.5,usb_cx+6.5,CY1-0.01,OY1+1,-0.1,7.5))

# ---------------- TAPA ----------------
plate = box(OX0,OX1,OY0,OY1,Z_WALL_TOP,PLATE_TOP).edges("|Z").fillet(3.0)
SW_CX = [122.81,142.0,161.31,180.5]
sw_x0, sw_x1 = 115.79-2.0, 187.52+2.0
sw_y0, sw_y1 = -116.06-2.0, -101.98+2.0
plate = plate.cut(box(sw_x0,sw_x1,sw_y0,sw_y1,Z_WALL_TOP-1,PLATE_TOP+1))
for (sx,sy) in SCREWS:
    plate = plate.cut(cq.Workplane("XY").circle(3.2/2).extrude(PLATE_T+2).translate((sx,sy,Z_WALL_TOP-1)))
# acceso a J1 (pantalla por cable)
j1x0,j1x1,j1y0,j1y1 = 115.69-2.0,136.01+2.0,-92.0-3.5,-92.0+3.5
plate = plate.cut(box(j1x0,j1x1,j1y0,j1y1,Z_WALL_TOP-1,PLATE_TOP+1))
# buzzer: nicho por debajo + agujeros de sonido
BZ = (183.0,-89.5)
pocket_top = 11.12+0.9
plate = plate.cut(cq.Workplane("XY").circle(13.0/2).extrude(pocket_top-(Z_WALL_TOP-1)).translate((BZ[0],BZ[1],Z_WALL_TOP-1)))
import math
pts=[(0,0)]+[(3.5*math.cos(math.radians(a)),3.5*math.sin(math.radians(a))) for a in range(0,360,60)]
for (dx,dy) in pts:
    plate = plate.cut(cq.Workplane("XY").circle(1.0).extrude(5).translate((BZ[0]+dx,BZ[1]+dy,pocket_top-0.5)))
# labio que sujeta la parte superior de la pantalla (detras de ella)
lip_x0,lip_x1 = PCB_CX-D_L/2+2, PCB_CX+D_L/2-2
lip = box(lip_x0,lip_x1,CY0,CY0+1.4,2.0,Z_WALL_TOP)
plate = plate.union(lip)

if __name__=="__main__":
    print("Z_FLOOR %.2f  Z_WALL_TOP %.2f  PLATE_TOP %.2f  BASE_BOT %.2f"%(Z_FLOOR,Z_WALL_TOP,PLATE_TOP,Z_BASE_BOT))
    print("OUTER  x %.1f..%.1f (%.1f)  y %.1f..%.1f (%.1f)  total height %.1f"%(OX0,OX1,OX1-OX0,OY0,OY1,OY1-OY0,PLATE_TOP-Z_BASE_BOT))
    print("display bottom %.2f top %.2f  center z %.2f  y glass %.2f back %.2f"%(D_BOTTOM,D_BOTTOM+D_H,D_ZC,D_YGLASS,D_YBACK))
    print("screws", [(round(a,2),round(b,2)) for a,b in SCREWS])
    cq.exporters.export(base,"case_base.step")
    cq.exporters.export(plate,"case_top_plate.step")
    print("base valid", base.val().isValid(), "vol %.0f"%base.val().Volume())
    print("plate valid", plate.val().isValid(), "vol %.0f"%plate.val().Volume())
