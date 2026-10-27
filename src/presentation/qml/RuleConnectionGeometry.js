.pragma library

// 화면 좌표의 연결점을 줌과 간격에 맞는 베지어 구간으로 바꾸는 함수.
function curveSegments(x1, y1, x2, y2, zoom) {
    const scale = Math.max(0.05, zoom)
    const dx = x2 - x1
    const dy = Math.abs(y2 - y1)
    if (dx >= 0) {
        // 가까운 노드 사이에서는 제어점이 서로 교차하지 않도록 제한한다.
        const handle = Math.min(dx / 2, Math.max(24 * scale, Math.min(180 * scale, dx * 0.45)))
        return [segment(x1, y1, x1 + handle, y1, x2 - handle, y2, x2, y2)]
    }
    const handle = Math.max(32 * scale, Math.min(160 * scale, -dx * 0.25 + dy * 0.25))
    if (dy >= 80 * scale)
        return [segment(x1, y1, x1 + handle, y1, x2 - handle, y2, x2, y2)]

    // 같은 높이의 역방향 연결은 직선 위에 접히지 않고 위쪽으로 부드럽게 돌아간다.
    const laneY = Math.min(y1, y2) - Math.min(160 * scale, 64 * scale - dx * 0.15)
    const middleX = (x1 + x2) / 2
    return [
        segment(x1, y1, x1 + handle, y1, x1 + handle, laneY, middleX, laneY),
        segment(middleX, laneY, x2 - handle, laneY, x2 - handle, y2, x2, y2)
    ]
}

// 베지어 한 구간의 시작점·제어점·끝점을 값으로 묶는 함수.
function segment(x1, y1, cx1, cy1, cx2, cy2, x2, y2) {
    return {x1: x1, y1: y1, cx1: cx1, cy1: cy1, cx2: cx2, cy2: cy2, x2: x2, y2: y2}
}

// 그리기에 사용하는 베지어 구간의 특정 위치를 계산하는 함수.
function pointAt(curve, t) {
    const inverse = 1 - t
    return {
        x: inverse * inverse * inverse * curve.x1 + 3 * inverse * inverse * t * curve.cx1
            + 3 * inverse * t * t * curve.cx2 + t * t * t * curve.x2,
        y: inverse * inverse * inverse * curve.y1 + 3 * inverse * inverse * t * curve.cy1
            + 3 * inverse * t * t * curve.cy2 + t * t * t * curve.y2
    }
}

// 포인터에서 직선 구간까지의 최단 거리를 계산하는 함수.
function distanceToSegment(px, py, x1, y1, x2, y2) {
    const dx = x2 - x1
    const dy = y2 - y1
    const squared = dx * dx + dy * dy
    const t = squared > 0 ? Math.max(0, Math.min(1, ((px - x1) * dx + (py - y1) * dy) / squared)) : 0
    return Math.hypot(px - (x1 + t * dx), py - (y1 + t * dy))
}

// 실제로 그린 것과 같은 곡선을 샘플링해 포인터까지의 거리를 구하는 함수.
function distanceToCurve(px, py, curves) {
    let nearest = Infinity
    for (const curve of curves) {
        let previous = {x: curve.x1, y: curve.y1}
        for (let step = 1; step <= 24; ++step) {
            const current = pointAt(curve, step / 24)
            nearest = Math.min(nearest, distanceToSegment(px, py, previous.x, previous.y, current.x, current.y))
            previous = current
        }
    }
    return nearest
}
