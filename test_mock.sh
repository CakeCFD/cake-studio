python3 "$(dirname "$0")/mock_server.py" 8765 &
SERVER_PID=$!
sleep 1

echo "=== /datasets ==="
curl -s http://localhost:8765/datasets | python3 -m json.tool | head -20

echo
echo "=== GetVelocity ==="
curl -s -X POST http://localhost:8765/ \
  -H 'Content-Type: text/xml; charset=utf-8' \
  -H 'SOAPAction: "http://turbulence.phy.jhu.edu/GetVelocity"' \
  -d '<soap:Envelope xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/"><soap:Body><GetVelocity xmlns="http://turbulence.phy.jhu.edu/"><authToken>test</authToken><dataset>mock_isotropic</dataset><time>0.5</time><points><Point3><x>0.5</x><y>0.5</y><z>0.5</z></Point3><Point3><x>1.0</x><y>0.2</y><z>0.3</z></Point3></points></GetVelocity></soap:Body></soap:Envelope>'

echo
echo "=== GetPressure ==="
curl -s -X POST http://localhost:8765/ \
  -H 'Content-Type: text/xml; charset=utf-8' \
  -H 'SOAPAction: "http://turbulence.phy.jhu.edu/GetPressure"' \
  -d '<soap:Envelope xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/"><soap:Body><GetPressure xmlns="http://turbulence.phy.jhu.edu/"><authToken>test</authToken><dataset>mock_isotropic</dataset><time>0.5</time><points><Point3><x>0.5</x><y>0.5</y><z>0.5</z></Point3></points></GetPressure></soap:Body></soap:Envelope>'

echo
echo "=== GetVelocityGradient ==="
curl -s -X POST http://localhost:8765/ \
  -H 'Content-Type: text/xml; charset=utf-8' \
  -H 'SOAPAction: "http://turbulence.phy.jhu.edu/GetVelocityGradient"' \
  -d '<soap:Envelope xmlns:soap="http://schemas.xmlsoap.org/soap/envelope/"><soap:Body><GetVelocityGradient xmlns="http://turbulence.phy.jhu.edu/"><authToken>test</authToken><dataset>mock_isotropic</dataset><time>0.5</time><points><Point3><x>0.5</x><y>0.5</y><z>0.5</z></Point3></points></GetVelocityGradient></soap:Body></soap:Envelope>'

echo
echo "=== /query (custom REST) ==="
curl -s -X POST http://localhost:8765/query \
  -H 'Content-Type: application/json' \
  -d '{"token":"test","dataset":"mock_isotropic","field":"velocity","time":0.5,"points":[{"x":0.5,"y":0.5,"z":0.5}]}'

kill $SERVER_PID 2>/dev/null
