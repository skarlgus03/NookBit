import QtQuick

// 화면·재생기·저장 서비스를 모르고 프레임별 개인 편집 초안과 좌표 계산만 담당한다.
QtObject {
    id: edits
    property var savedEdits: ({})
    property var step: ({})
    property var steps: []
    property var animationId
    property var bubble: ({})
    property bool editing: false
    property bool frameReady: false
    property point userPivotOffset: Qt.point(0, 0)
    property point userBubbleOffset: Qt.point(0, 0)
    property var drafts: ({})
    property var durationDrafts: ({})
    readonly property string frameKey: step.frameId || ""
    readonly property var draft: drafts[frameKey] || ({})
    readonly property var basePivot: draft.pivot || step.pivot || ({x: 0, y: 0})
    readonly property var pivot: ({x: basePivot.x + userPivotOffset.x, y: basePivot.y + userPivotOffset.y})
    readonly property var baseAttachment: draft.attachment || bubble.attachment || ({x: 0, y: -120})
    readonly property var attachment: ({
        x: baseAttachment.x + userBubbleOffset.x - userPivotOffset.x,
        y: baseAttachment.y + userBubbleOffset.y - userPivotOffset.y
    })
    // 저장값의 복원을 먼저 평가하고 현재 초안과 비교하는 속성.
    readonly property bool dirty: {
        const frames = JSON.stringify(savedEdits.frames || ({}));
        const durations = JSON.stringify(savedEdits.durations || ({}));
        return frames !== JSON.stringify(drafts) || durations !== JSON.stringify(durationDrafts);
    }
    onSavedEditsChanged: reloadEdits()
    Component.onCompleted: reloadEdits()

    // 저장된 값과 객체를 공유하지 않는 독립적인 초안으로 복원하는 함수.
    function reloadEdits() {
        drafts = JSON.parse(JSON.stringify(savedEdits.frames || ({})));
        durationDrafts = JSON.parse(JSON.stringify(savedEdits.durations || ({})));
    }
    // 프레임별 기준점과 지속시간 초안을 모두 팩 기본값으로 돌리는 함수.
    function resetEdits() {
        drafts = ({});
        durationDrafts = ({});
    }
    // 해석된 원본 프레임을 바꾸지 않고 지속시간 초안을 재생 설명에 덧씌우는 함수.
    function withDurationDrafts(authored, resolvedSteps) {
        const changes = durationDrafts[authored.id];
        if (!changes) return authored;
        const updated = resolvedSteps.map((frame, index) => changes[index] === undefined ? frame
            : Object.assign({}, frame, {durationMs: changes[index]}));
        return Object.assign({}, authored, {steps: updated});
    }
    // 선택 애니메이션의 특정 등장 순서에만 지속시간 초안을 남기는 함수.
    function resizeFrame(index, durationMs) {
        if (!editing || !Number.isInteger(index) || index < 0 || index >= steps.length || !isFinite(durationMs))
            return false;
        const all = Object.assign({}, durationDrafts);
        const changes = Object.assign({}, all[animationId] || {});
        changes[index] = Math.max(16, Math.min(60000, Math.round(durationMs)));
        all[animationId] = changes;
        durationDrafts = all;
        return true;
    }
    // 이미지 논리 좌표를 공통 보정값과 분리된 프레임 개인값으로 기록하는 함수.
    function movePoint(kind, x, y) {
        if (!editing || !frameReady) return;
        const values = Object.assign({}, drafts);
        const value = Object.assign({}, values[frameKey] || {});
        if (kind === 0)
            value.pivot = {x: x - userPivotOffset.x, y: y - userPivotOffset.y};
        else
            value.attachment = {x: x - pivot.x - userBubbleOffset.x + userPivotOffset.x,
                y: y - pivot.y - userBubbleOffset.y + userPivotOffset.y};
        values[frameKey] = value;
        drafts = values;
    }
    // 숫자로 입력한 기준점과 말풍선 부착점을 같은 초안에 반영하는 함수.
    function setCoordinates(px, py, ax, ay) {
        if (!editing || ![px, py, ax, ay].every(isFinite)) return;
        movePoint(0, px, py);
        movePoint(1, ax, ay);
    }
    // 선택한 애니메이션의 프레임에 현재 기준점 또는 부착점만 정렬하는 함수.
    function alignFrames(kind) {
        if (!editing || !frameReady || ![0, 1, 2].includes(kind)) return;
        const sourcePivot = {x: basePivot.x, y: basePivot.y};
        const bubblePoint = {x: basePivot.x + baseAttachment.x, y: basePivot.y + baseAttachment.y};
        const values = Object.assign({}, drafts);
        for (const frame of steps) {
            const value = Object.assign({}, values[frame.frameId] || {});
            if (kind === 0 || kind === 2) value.pivot = {x: sourcePivot.x, y: sourcePivot.y};
            const targetPivot = value.pivot || frame.pivot;
            if (kind === 1 || kind === 2)
                value.attachment = {x: bubblePoint.x - targetPivot.x, y: bubblePoint.y - targetPivot.y};
            values[frame.frameId] = value;
        }
        drafts = values;
    }
}
