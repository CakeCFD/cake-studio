import html, math, random, re, json
from http.server import HTTPServer, BaseHTTPRequestHandler

PORT = 8765
SOAP_PATH = '/turbulence.asmx'
JHTDB_NS  = 'http://turbulence.pha.jhu.edu/'

# JHTDB SpatialInterpolation / TemporalInterpolation enum members
SPATIAL_INTERP = {
    'None', 'Lag4', 'Lag6', 'Lag8',
    'None_Fd4', 'None_Fd6', 'None_Fd8', 'Fd4Lag4',
    'M1Q4', 'M1Q6', 'M1Q8', 'M1Q10', 'M1Q12', 'M1Q14',
    'M2Q4', 'M2Q6', 'M2Q8', 'M2Q10', 'M2Q12', 'M2Q14',
    'M3Q4', 'M3Q6', 'M3Q8', 'M3Q10', 'M3Q12', 'M3Q14',
    'M4Q4', 'M4Q6', 'M4Q8', 'M4Q10', 'M4Q12', 'M4Q14',
}
TEMPORAL_INTERP = {'None', 'PCHIP'}

# synthetic field generators

def _turb_velocity(x, y, z, t):
    """Fake turbulent velocity - sinusoidal with seeded noise."""
    rng = random.Random(hash((round(x,3), round(y,3), round(z,3))))
    noise = lambda: rng.uniform(-0.05, 0.05)
    vx = 1.0 * math.sin(2*math.pi*x) * math.cos(math.pi*y) + noise()
    vy = 0.3 * math.cos(math.pi*x) * math.sin(2*math.pi*z) + noise()
    vz = 0.1 * math.sin(math.pi*(x+z)) + noise()
    return vx, vy, vz

def _pressure(x, y, z, t):
    rng = random.Random(hash((round(x,3), round(y,3), round(z,3), 'p')))
    return 101325.0 + 50.0 * math.sin(math.pi*x*y) + rng.uniform(-10, 10)

def _vorticity(x, y, z, t):
    vx, vy, vz = _turb_velocity(x, y, z, t)
    return -vz*0.5, vx*0.3, -vy*0.4   # rough curl approximation

def _magnetic(x, y, z, t):
    return 0.01*math.sin(x), 0.01*math.cos(y), 0.005*math.sin(z)

def _temperature(x, y, z, t):
    return 300.0 + 5.0 * math.sin(math.pi*x) * math.cos(math.pi*y)

def _vel_gradient(x, y, z, t):
    # d/dx of (vx,vy,vz) approximated analytically from _turb_velocity
    dx = 1e-4
    vx0,vy0,vz0 = _turb_velocity(x,    y, z, t)
    vxX,vyX,vzX = _turb_velocity(x+dx, y, z, t)
    vxY,vyY,vzY = _turb_velocity(x, y+dx, z, t)
    vxZ,vyZ,vzZ = _turb_velocity(x, y, z+dx, t)
    duxdx=(vxX-vx0)/dx; duxdy=(vxY-vx0)/dx; duxdz=(vxZ-vx0)/dx
    duydx=(vyX-vy0)/dx; duydy=(vyY-vy0)/dx; duydz=(vyZ-vy0)/dx
    duzdx=(vzX-vz0)/dx; duzdy=(vzY-vz0)/dx; duzdz=(vzZ-vz0)/dx
    return duxdx,duxdy,duxdz,duydx,duydy,duydz,duzdx,duzdy,duzdz

def _pressure_gradient(x, y, z, t):
    dx = 1e-4
    p0 = _pressure(x, y, z, t)
    dpx = (_pressure(x+dx, y, z, t) - p0) / dx
    dpy = (_pressure(x, y+dx, z, t) - p0) / dx
    dpz = (_pressure(x, y, z+dx, t) - p0) / dx
    return dpx, dpy, dpz

# SOAP helpers

def _parse_points(body: str):
    """Extract list of (x,y,z) from SOAP request body."""
    pts = []
    for m in re.finditer(r'<(?:\w+:)?Point3[^>]*>(.*?)</(?:\w+:)?Point3>', body, re.DOTALL):
        chunk = m.group(1)
        def tag(n): return re.search(rf'<(?:\w+:)?{n}>(.*?)</(?:\w+:)?{n}>', chunk)
        mx = tag('x'); my = tag('y'); mz = tag('z')
        if mx and my and mz:
            pts.append((float(mx.group(1)), float(my.group(1)), float(mz.group(1))))
    return pts

def _parse_elem(body: str, name: str):
    m = re.search(rf'<(?:\w+:)?{name}>(.*?)</(?:\w+:)?{name}>', body, re.DOTALL)
    return m.group(1).strip() if m else None

def _parse_time(body: str):
    v = _parse_elem(body, 'time')
    return float(v) if v else 0.0

def _soap_fault(msg: str) -> bytes:
    return (
        '<?xml version="1.0" encoding="utf-8"?>'
        '<soap:Envelope xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/">'
        '<soap:Body><soap:Fault><faultcode>soap:Client</faultcode>'
        f'<faultstring>{html.escape(msg)}</faultstring>'
        '</soap:Fault></soap:Body></soap:Envelope>'
    ).encode()

def _soap_wrap(method: str, inner: str) -> bytes:
    # Matches the real JHTDB response envelope exactly
    # <{Method}Response xmlns="http://turbulence.pha.jhu.edu/">
    #   <{Method}Result> ... structs with bare element names ... </{Method}Result>
    # </...Response>
    return (
        '<?xml version="1.0" encoding="utf-8"?>'
        '<soap:Envelope xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/">'
        '<soap:Body>'
        f'<{method}Response xmlns="{JHTDB_NS}">'
        f'<{method}Result>'
        + inner +
        f'</{method}Result>'
        f'</{method}Response>'
        '</soap:Body>'
        '</soap:Envelope>'
    ).encode()

def _handle_soap(body: str, method: str):
    """Returns (http_status, xml_bytes)."""
    # Interpolation elements are optional here (curl tests omit them) but,
    # when present, must use the WSDL element names and enum values
    for legacy in ('spatialInterp', 'temporalInterp'):
        if _parse_elem(body, legacy) is not None:
            return 500, _soap_fault(f'Unknown element <{legacy}>: use '
                                    f'<{legacy}olation> with an enum name')
    si = _parse_elem(body, 'spatialInterpolation')
    ti = _parse_elem(body, 'temporalInterpolation')
    if si is not None and si not in SPATIAL_INTERP:
        return 500, _soap_fault(f"Invalid SpatialInterpolation value '{si}'")
    if ti is not None and ti not in TEMPORAL_INTERP:
        return 500, _soap_fault(f"Invalid TemporalInterpolation value '{ti}'")
    return 200, _handle_soap_points(body, method)

def _handle_soap_points(body: str, method: str) -> bytes:
    pts = _parse_points(body)
    t   = _parse_time(body)
    if not pts:
        return _soap_wrap(method, '<tns:Error>No points found in request</tns:Error>')

    chunks = []
    for x, y, z in pts:
        if method == 'GetVelocity':
            vx,vy,vz = _turb_velocity(x,y,z,t)
            chunks.append(
                f'<Vector3>'
                f'<x>{vx:.6f}</x><y>{vy:.6f}</y><z>{vz:.6f}</z>'
                f'</Vector3>')
        elif method == 'GetPressure':
            # Real JHTDB wraps it as <Pressure><p>float</p></Pressure>
            chunks.append(f'<Pressure><p>{_pressure(x,y,z,t):.6f}</p></Pressure>')
        elif method == 'GetVorticity':
            wx,wy,wz = _vorticity(x,y,z,t)
            chunks.append(
                f'<Vector3>'
                f'<x>{wx:.6f}</x><y>{wy:.6f}</y><z>{wz:.6f}</z>'
                f'</Vector3>')
        elif method == 'GetMagneticField':
            bx,by,bz = _magnetic(x,y,z,t)
            chunks.append(
                f'<Vector3>'
                f'<x>{bx:.6f}</x><y>{by:.6f}</y><z>{bz:.6f}</z>'
                f'</Vector3>')
        elif method == 'GetTemperature':
            # Real JHTDB wraps it as <Temperature><p>float</p></Temperature>
            chunks.append(f'<Temperature><p>{_temperature(x,y,z,t):.6f}</p></Temperature>')
        elif method == 'GetVelocityGradient':
            g = _vel_gradient(x,y,z,t)
            names = ['duxdx','duxdy','duxdz','duydx','duydy','duydz','duzdx','duzdy','duzdz']
            inner = ''.join(f'<{n}>{v:.6f}</{n}>' for n,v in zip(names,g))
            chunks.append(f'<VelocityGradient>{inner}</VelocityGradient>')
        elif method == 'GetPressureGradient':
            dpx,dpy,dpz = _pressure_gradient(x,y,z,t)
            chunks.append(
                f'<Vector3>'
                f'<x>{dpx:.6f}</x><y>{dpy:.6f}</y><z>{dpz:.6f}</z>'
                f'</Vector3>')
        else:
            return _soap_wrap(method, f'<tns:Error>Unknown method: {method}</tns:Error>')

    return _soap_wrap(method, ''.join(chunks))

# mock datasets

DATASETS = [
    {
        "id": "mock_isotropic",
        "description": "Mock isotropic turbulence (Re_λ=433)",
        "fields": ["velocity","pressure","vorticity","velocity_gradient","pressure_gradient"],
        "tMin": 0.0, "tMax": 10.0, "dt": 0.002
    },
    {
        "id": "mock_channel",
        "description": "Mock channel flow (Re_τ=1000)",
        "fields": ["velocity","pressure","temperature","vorticity"],
        "tMin": 0.0, "tMax": 26.0, "dt": 0.0065
    },
    {
        "id": "mock_mhd",
        "description": "Mock MHD turbulence",
        "fields": ["velocity","magnetic","pressure"],
        "tMin": 0.0, "tMax": 2.56, "dt": 0.0025
    },
]

# HTTP handler

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[mock] {self.address_string()} {fmt % args}")

    def _send(self, code, content_type, body: bytes):
        self.send_response(code)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type,Authorization,SOAPAction')
        self.send_header('Access-Control-Allow-Methods', 'GET,POST,OPTIONS')
        self.end_headers()

    def do_GET(self):
        if self.path == '/datasets':
            self._send(200, 'application/json', json.dumps(DATASETS).encode())
        elif self.path in ('/', '/health', SOAP_PATH):
            self._send(200, 'text/plain', b'CakeFOAM mock turbulence server - OK')
        else:
            self._send(404, 'text/plain', b'Not found')

    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        body   = self.rfile.read(length).decode('utf-8', errors='replace')

        if self.path == '/query':
            # Custom REST API mode
            try:
                req = json.loads(body)
            except json.JSONDecodeError:
                self._send(400, 'application/json', b'{"error":"bad json"}')
                return
            pts_raw = req.get('points', [])
            field   = req.get('field', 'velocity')
            t       = float(req.get('time', 0.0))
            results = []
            for p in pts_raw:
                x,y,z = float(p['x']), float(p['y']), float(p['z'])
                if field == 'velocity':
                    vx,vy,vz = _turb_velocity(x,y,z,t)
                    results.append({'vx':vx,'vy':vy,'vz':vz})
                elif field == 'pressure':
                    results.append({'p': _pressure(x,y,z,t)})
                elif field == 'temperature':
                    results.append({'T': _temperature(x,y,z,t)})
                else:
                    vx,vy,vz = _turb_velocity(x,y,z,t)
                    results.append({'vx':vx,'vy':vy,'vz':vz})
            self._send(200, 'application/json', json.dumps({'results': results}).encode())
            return

        # SOAP dispatch (SOAP_PATH, or any other path for curl tests) -
        # method from the SOAPAction header ("<namespace>GetVelocity") or body
        soap_action = self.headers.get('SOAPAction', '').strip('"').split('/')[-1]
        if not soap_action:
            m = re.search(r'<(?:\w+:)?(Get\w+)[\s>]', body)
            soap_action = m.group(1) if m else 'GetVelocity'

        status, resp = _handle_soap(body, soap_action)
        self._send(status, 'text/xml; charset=utf-8', resp)


if __name__ == '__main__':
    import sys
    port = int(sys.argv[1]) if len(sys.argv) > 1 else PORT
    server = HTTPServer(('0.0.0.0', port), Handler)
    print(f"Mock turbulence server running on http://localhost:{port}/")
    print(f"  SOAP endpoint  →  http://localhost:{port}{SOAP_PATH}")
    print(f"  Dataset list   →  http://localhost:{port}/datasets")
    print(f"  Custom query   →  http://localhost:{port}/query  (POST JSON; endpoint http://localhost:{port})")
    print("Press Ctrl-C to stop.\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
