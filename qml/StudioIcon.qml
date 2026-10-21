import QtQuick

Image {
    id: root
    property string name: "image"
    property color ink: StudioTheme.secondary
    sourceSize.width: 48
    sourceSize.height: 48
    width: 20
    height: 20
    fillMode: Image.PreserveAspectFit
    readonly property var paths: ({
        search: '<circle cx="10" cy="10" r="7"/><path d="m15 15 6 6"/>',
        document: '<path d="M5 2h10l5 5v15H5Zm10 0v6h5M8 12h8M8 16h8"/>',
        list: '<path d="M4 6h16M4 12h16M4 18h16"/>',
        help: '<circle cx="12" cy="12" r="10"/><path d="M9 8a3 3 0 0 1 6 0c0 2-3 2-3 5m0 4h.01"/>',
        contact: '<path d="M4 14v-3a8 8 0 0 1 16 0v3M4 12H2v7h4v-7Zm16 0h2v7h-4v-7Zm0 7c0 3-3 3-6 3"/>',
        info: '<circle cx="12" cy="12" r="10"/><path d="M12 11v6m0-10h.01"/>',
        gear: '<path d="m9 3 1-2h4l1 2 3 2 3 0 2 4-2 3 0 3-3 3-2 3h-5l-2-3-3-1-3-3 0-4 2-2 1-3Z"/><circle cx="12" cy="11" r="4"/>',
        trash: '<path d="M3 6h18M9 6V3h6v3M6 6l1 16h10l1-16M10 10v8m4-8v8"/>',
        copy: '<rect x="8" y="8" width="12" height="13" rx="2"/><path d="M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h3"/>',
        refresh: '<path d="M20 7v5h-5M4 17v-5h5M5.2 8a7.5 7.5 0 0 1 12.4-3L20 8M4 16l2.4 3a7.5 7.5 0 0 0 12.4-3"/>',
        download: '<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
        server: '<rect x="3" y="3" width="18" height="7" rx="2"/><rect x="3" y="14" width="18" height="7" rx="2"/><path d="M7 6.5h.01M7 17.5h.01M11 6.5h6M11 17.5h6"/>',
        image: '<rect x="3" y="3" width="18" height="18" rx="4"/><circle cx="8.5" cy="8" r="1.5"/><path d="m4 17 5-5 4 4 3-3 5 5"/>',
        layers: '<path d="m12 3 10 5-10 5L2 8Zm-9 10 9 5 9-5M3 18l9 5 9-5"/>',
        brush: '<path d="m14 4 3-2 5 5-2 3-8 8-5-5Zm-7 9 5 5M7 15c-5-1-5 4-5 7 3 0 8 0 7-5"/>',
        person: '<circle cx="12" cy="8" r="4"/><path d="M4 22v-3a8 8 0 0 1 16 0v3M19 5l2 2-2 2"/>',
        grid: '<rect x="3" y="3" width="7" height="7" rx="2"/><rect x="14" y="3" width="7" height="7" rx="2"/><rect x="3" y="14" width="7" height="7" rx="2"/><rect x="14" y="14" width="7" height="7" rx="2"/>',
        model: '<path d="m12 2 9 5v10l-9 5-9-5V7Zm0 10v10M3 7l9 5 9-5M7 5l10 6"/>',
        folder: '<path d="M3 7V4h7l3 3h8v13H3Zm0 4h18"/>',
        save: '<path d="M4 3h13l4 4v14H3V3Zm3 0v7h10V3M7 21v-7h10v7"/>',
        export: '<path d="M12 16V2m-5 5 5-5 5 5M4 13v8h16v-8"/>',
        arrow: '<path d="M4 12h16m-6-6 6 6-6 6"/>',
        select: '<path d="m5 3 14 11-7 1-3 7Z"/>',
        rectangle: '<rect x="3" y="5" width="18" height="14" rx="2" stroke-dasharray="3 3"/>',
        eraser: '<path d="m14 3 7 7-11 11H5l-4-4Zm-9 10 7 7m-3 1h13"/>',
        text: '<path d="M4 6V3h16v3M12 3v18M8 21h8"/>',
        plus: '<path d="M12 4v16M4 12h16"/>',
        minus: '<path d="M4 12h16"/>',
        edit: '<path d="m15 3 6 6-12 12H3v-6Zm-1 1 6 6M3 15l6 6"/>',
        undo: '<path d="m8 4-5 5 5 5M3 9h11a7 7 0 0 1 0 14"/>',
        redo: '<path d="m16 4 5 5-5 5m5-5H10a7 7 0 0 0 0 14"/>',
        expand: '<path d="M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5"/>',
        chevron: '<path d="m7 10 5 5 5-5"/>',
        more: '<circle cx="4" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="20" cy="12" r="1"/>',
        spark: '<path d="m12 2 3 7 7 3-7 3-3 7-3-7-7-3 7-3Zm7 0v4m-2-2h4"/>',
        check: '<path d="m5 12 4 4L20 5"/>',
        play: '<path d="m7 3 14 9-14 9Z"/>',
        pause: '<path d="M8 4v16M16 4v16"/>',
        close: '<path d="m6 6 12 12M6 18 18 6"/>',
        background: '<rect x="3" y="3" width="18" height="18" rx="3"/><path d="M4 16 16 4M9 21 21 9M3 10l7-7"/>',
        volume: '<path d="m3 9 5 0 5-5v16l-5-5H3Zm14-2a7 7 0 0 1 0 10m3-13a11 11 0 0 1 0 16"/>',
        muted: '<path d="m3 9 5 0 5-5v16l-5-5H3Zm14 0 5 6m0-6-5 6"/>',
        audio: '<path d="M3 10v4m4-8v12m5-15v18m5-15v12m4-8v4"/>',
        monitor: '<rect x="2" y="3" width="20" height="14" rx="3"/><path d="M12 17v5m-5 0h10"/>'
        ,sidebar: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M9 3v18"/>'
        ,settings: '<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="2"/><circle cx="15" cy="17" r="2"/>'
    })
    source: "data:image/svg+xml;utf8," + encodeURIComponent(
        '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="'
        + Qt.rgba(ink.r,ink.g,ink.b,1) + '" stroke-opacity="' + ink.a + '" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">'
        + (paths[name] || paths.image) + '</svg>')
}
