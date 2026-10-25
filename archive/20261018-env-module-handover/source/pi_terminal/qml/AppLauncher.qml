import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Terminal.Native 1.0

Item {
    id:root
    property var model
    property int revision:0
    property Item backgroundSource
    // One page is one large card. Adjacent pages remain visible during a swipe.
    readonly property int columns:1
    readonly property int rows:1
    readonly property int capacity:columns*rows
    readonly property int pageCount:Math.max(1,Math.ceil((model?model.count:0)/capacity))
    property alias currentIndex:view.currentIndex
    property alias contentX:view.contentX
    signal activated(var entry,real originX,real originY)
    FontMetrics {id:titleMetrics;font.pointSize:Theme.title}
    FontMetrics {id:bodyMetrics;font.pointSize:Theme.body}
    function entryAt(index) {
        if(!model || index<0 || index>=model.count) return {entryId:"",title:"",description:"",category:"",accentColor:"#85deff",iconName:"extension",route:0,instance:"",ready:false}
        let e=model.get(index)
        return {entryId:e.entryId,title:e.title,description:e.description,category:e.category,accentColor:e.accentColor,iconName:e.iconName,route:e.route,instance:e.instance,ready:e.ready}
    }
    function reveal(instance) {
        for(let i=0;i<(model?model.count:0);++i) if(model.get(i).instance===instance) {view.currentIndex=Math.floor(i/capacity);return}
    }
    onPageCountChanged:if(view.currentIndex>=pageCount)view.currentIndex=pageCount-1
    Flickable {
        id:vertical;objectName:"homePage";anchors.fill:parent;clip:true
        contentWidth:width;contentHeight:Math.max(height,root.rows*224*Theme.scaleFactor+64*Theme.scaleFactor)
        boundsBehavior:Flickable.StopAtBounds;flickableDirection:Flickable.VerticalFlick
        ListView {
            id:view;objectName:"homeCarousel";width:parent.width;height:vertical.contentHeight-64*Theme.scaleFactor
            orientation:ListView.Horizontal;clip:true;model:root.pageCount
            snapMode:ListView.SnapOneItem;boundsBehavior:Flickable.StopAtBounds
            highlightRangeMode:ListView.StrictlyEnforceRange;preferredHighlightBegin:0;preferredHighlightEnd:width
            highlightMoveDuration:240;maximumFlickVelocity:2000*Theme.scaleFactor;flickDeceleration:2800*Theme.scaleFactor
            focus:true
            Keys.onLeftPressed:decrementCurrentIndex()
            Keys.onRightPressed:incrementCurrentIndex()
            delegate:Item {
                id:page;required property int index;width:view.width;height:view.height
                Repeater {
                    model:root.capacity
                    delegate:Item {
                        id:tile;objectName:"appTile";required property int index
                        property var entry:{let changed=root.revision;return root.entryAt(page.index*root.capacity+index)}
                        property string entryId:entry.entryId
                        property bool auditTile:true
                        width:Math.min(page.width*0.68,680*Theme.scaleFactor)
                        height:Math.min(page.height*0.82,470*Theme.scaleFactor)
                        x:(page.width-width)/2
                        y:(page.height-height)/2
                        visible:entryId!==""
                        property real rotationDegrees:Math.max(-30,Math.min(30,((page.x-view.contentX+x+width/2)-view.width/2)/Math.max(1,view.width)*26))
                        property real iconSize:Math.min(128*Theme.scaleFactor,Math.max(80*Theme.scaleFactor,height-titleMetrics.height-bodyMetrics.height-Theme.gap*3))
                        function launch() {
                            let point=iconSurface.mapToItem(null,iconSurface.width/2,iconSurface.height/2)
                            root.activated(entry,point.x,point.y)
                        }
                        activeFocusOnTab:true
                        Keys.onReturnPressed:launch()
                        Accessible.role:Accessible.Button;Accessible.name:entry.title+" "+entry.description
                        Accessible.onPressAction:launch()
                        transform:Rotation { objectName:"cardZRotation"; origin.x:tile.width/2; origin.y:tile.height/2; axis.x:0; axis.y:0; axis.z:1; angle:tile.rotationDegrees }
                        ShaderEffectSource {
                            id:cardBackdrop;sourceItem:root.backgroundSource;visible:false;live:true
                            sourceRect:{
                                const horizontal=view.contentX,verticalOffset=vertical.contentY,angle=tile.rotationDegrees
                                if(!root.backgroundSource)return Qt.rect(0,0,1,1)
                                const center=tile.mapToItem(root.backgroundSource,tile.width/2,tile.height/2)
                                return Qt.rect(center.x-tile.width/2,center.y-tile.height/2,tile.width,tile.height)
                            }
                            textureSize:Qt.size(Math.max(1,tile.width),Math.max(1,tile.height))
                        }
                        BlurItem {objectName:"launcherBackdropBlur";anchors.fill:parent;source:cardBackdrop;levels:4;strength:2;mixAmount:1;cornerRadius:28*Theme.scaleFactor}
                        Item {
                            objectName:"launcherCardChrome";anchors.fill:parent;opacity:systemTools.launcherOpacity;layer.enabled:true
                        Rectangle {
                            id:cardSurface; anchors.fill:parent; radius:28*Theme.scaleFactor
                            color:Theme.surface; border.color:page.index===view.currentIndex?Theme.accent:Theme.outline
                            border.width:page.index===view.currentIndex?3:1
                            gradient:Gradient {
                                GradientStop {position:0;color:tile.entry.accentColor}
                                GradientStop {position:0.16;color:Theme.surface}
                                GradientStop {position:1;color:Theme.background}
                            }
                        }
                        BlurItem { objectName:"launcherCardBlur";anchors.fill:cardSurface;source:cardSurface;levels:1;strength:0.35;mixAmount:0.12;opacity:0.9 }
                        }
                        Column {
                            anchors.centerIn:parent;width:parent.width;spacing:Theme.gap
                            Item {
                                objectName:"launcherLogo"
                                width:tile.iconSize;height:width;anchors.horizontalCenter:parent.horizontalCenter
                                scale:tap.pressed?0.94:1
                                Behavior on scale {NumberAnimation{duration:90}}
                                transform:Rotation {objectName:"cardZRotation";origin.x:tile.iconSize/2;origin.y:tile.iconSize/2
                                    axis.x:0;axis.y:0;axis.z:1;angle:tile.rotationDegrees}
                                Rectangle {
                                    id:iconSurface;anchors.fill:parent;radius:width/2;color:tile.entry.accentColor;layer.enabled:true
                                    MaterialIcon {anchors.centerIn:parent;width:parent.width*0.55;height:width;name:tile.entry.iconName;light:false}
                                }
                                // Blend a shallow GPU blur with 90% of the original glyph. Labels stay sharp.
                                BlurItem {objectName:"launcherIconBlur";anchors.fill:parent;source:iconSurface;levels:1;strength:0.35;mixAmount:0.10}
                                Rectangle {anchors.fill:parent;anchors.margins:-5*Theme.scaleFactor;radius:width/2;color:"transparent"
                                    border.width:tile.activeFocus?2:0;border.color:Theme.accent}
                                Rectangle {
                                    opacity:systemTools.launcherOpacity;visible:tile.entry.instance!=="";width:32*Theme.scaleFactor;height:width;radius:width/2
                                    anchors.right:parent.right;anchors.bottom:parent.bottom;color:Theme.surface
                                    MaterialIcon {anchors.centerIn:parent;width:24*Theme.scaleFactor;height:width;name:tile.entry.ready?"check":"refresh"}
                                }
                            }
                            CarText {id:nameText;objectName:"launcherTitle";opacity:systemTools.launcherOpacity;width:parent.width;height:titleMetrics.height;text:tile.entry.title;heading:true;horizontalAlignment:Text.AlignHCenter
                                maximumLineCount:1;elide:Text.ElideRight}
                            CarText {id:descriptionText;objectName:"launcherDescription";opacity:systemTools.launcherOpacity;width:parent.width;height:bodyMetrics.height;text:tile.entry.description||"点击打开功能";subdued:true
                                Accessible.ignored:tile.entry.instance==="";horizontalAlignment:Text.AlignHCenter;maximumLineCount:1;elide:Text.ElideRight}
                        }
                        TapHandler {id:tap;gesturePolicy:TapHandler.DragThreshold;onTapped:tile.launch()}
                    }
                }
            }
        }
        RowLayout {
            visible:root.pageCount>1
            anchors.top:view.bottom;anchors.horizontalCenter:parent.horizontalCenter;height:64*Theme.scaleFactor;spacing:Theme.gap
            IconButton {visible:root.width>420*Theme.scaleFactor;iconName:"chevron_left";label:"上一页应用";enabled:view.currentIndex>0;onClicked:view.decrementCurrentIndex()}
            CarText {text:(view.currentIndex+1)+" / "+root.pageCount;subdued:true;horizontalAlignment:Text.AlignHCenter}
            IconButton {visible:root.width>420*Theme.scaleFactor;iconName:"chevron_right";label:"下一页应用";enabled:view.currentIndex<root.pageCount-1;onClicked:view.incrementCurrentIndex()}
        }
    }
}
