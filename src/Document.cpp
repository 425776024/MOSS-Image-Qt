#include "Document.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <cmath>

Document::Document(QObject *p):QObject(p) {connect(this,&Document::changed,this,&Document::selectionChanged);}
void Document::remember() {
    m_undo.append(json()); if(m_undo.size()>100) m_undo.removeFirst(); m_redo.clear();
}
void Document::setPrompt(QString v) { if(v==m_prompt)return; remember(); m_prompt=v; emit changed(); }
void Document::setNegativePrompt(QString v){if(v==m_negative)return;remember();m_negative=v;emit changed();}
void Document::setStyle(QString v){if(v==m_style)return;remember();m_style=v;emit changed();}
void Document::setLayoutMeta(QString v){
    auto meta=v.split(',',Qt::SkipEmptyParts);for(auto &s:meta)s=s.trimmed();
    if(meta==m_meta)return;remember();m_meta=meta;emit changed();
}
void Document::setSelected(int v) { v=v>=0&&v<m_regions.size()?v:-1; if(v!=m_selected){m_selected=v;emit selectionChanged();} }
void Document::setLayout(bool v) { if(v!=m_layout){remember();m_layout=v;emit changed();} }
void Document::setCanvasWidth(int v) {
    setCanvasSize(v,m_height);
}
void Document::setCanvasHeight(int v) {
    setCanvasSize(m_width,v);
}
void Document::setCanvasSize(int w,int h){
    if(w<256||w>4096||h<256||h>4096||qint64(w)*h>4194304||w%16||h%16||(w==m_width&&h==m_height))return;
    remember();const double sx=double(w)/m_width,sy=double(h)/m_height;
    for(auto &item:m_regions){auto r=item.toMap();
        const double rw=std::max(4.,r["width"].toDouble()*sx),rh=std::max(4.,r["height"].toDouble()*sy);
        r["x"]=std::min(r["x"].toDouble()*sx,w-rw);r["width"]=rw;
        r["y"]=std::min(r["y"].toDouble()*sy,h-rh);r["height"]=rh;item=r;
    }
    m_width=w;m_height=h;emit changed();
}
void Document::addRegion(double x,double y,double w,double h,QString type) {
    if(m_regions.size()>=100||!std::isfinite(x+y+w+h)||w<4||h<4)return;
    remember();x=std::clamp(x,0.,double(m_width)-4);y=std::clamp(y,0.,double(m_height)-4);
    static const QStringList colors={"#5878E8","#E99B4B","#55A58A","#A27BD2","#D97790"};
    m_regions.append(QVariantMap{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"type",type=="text"?"text":"object"},{"name",QString(type=="text"?"文字 %1":"对象 %1").arg(m_regions.size()+1)},
        {"caption",""},{"text",""},{"x",x},{"y",y},{"width",std::min(w,m_width-x)},
        {"height",std::min(h,m_height-y)},{"color",colors[m_regions.size()%colors.size()]}});
    m_selected=m_regions.size()-1;emit changed();
}
void Document::updateRegion(int i,QString field,QVariant value) {
    if(i<0||i>=m_regions.size()||!QStringList{"name","caption","text","type"}.contains(field))return;
    if(field=="type"&&value!="text"&&value!="object")return;
    auto r=m_regions[i].toMap();if(r[field]==value)return;remember();r[field]=value;m_regions[i]=r;emit changed();
}
void Document::moveRegion(int i,double x,double y,double w,double h) {
    if(i<0||i>=m_regions.size()||!std::isfinite(x+y+w+h))return;
    w=std::clamp(w,4.,double(m_width));h=std::clamp(h,4.,double(m_height));
    auto r=m_regions[i].toMap();auto before=r;r["x"]=std::clamp(x,0.,m_width-w);r["y"]=std::clamp(y,0.,m_height-h);
    r["width"]=w;r["height"]=h;if(r==before)return;remember();
    r["width"]=w;r["height"]=h;m_regions[i]=r;emit changed();
}
void Document::removeSelected(){if(m_selected<0)return;remember();m_regions.removeAt(m_selected);m_selected=-1;emit changed();}
void Document::duplicateSelected(){
    if(m_selected<0||m_regions.size()>=100)return;remember();auto r=m_regions[m_selected].toMap();
    r["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);r["name"]=r["name"].toString()+" 副本";
    r["x"]=std::min(r["x"].toDouble()+24,m_width-r["width"].toDouble());
    r["y"]=std::min(r["y"].toDouble()+24,m_height-r["height"].toDouble());
    m_regions.append(r);m_selected=m_regions.size()-1;emit changed();
}
QJsonObject Document::json() const {
    return {{"version",1},{"prompt",m_prompt},{"negativePrompt",m_negative},{"style",m_style},
        {"layoutMeta",QJsonArray::fromStringList(m_meta)},{"width",m_width},{"height",m_height},{"layout",m_layout},{"regions",QJsonArray::fromVariantList(m_regions)}};
}
void Document::restore(const QJsonObject &o) {
    m_prompt=o["prompt"].toString();m_width=o["width"].toInt();m_height=o["height"].toInt();
    m_negative=o["negativePrompt"].toString();m_style=o["style"].toString();m_meta.clear();
    for(const auto &v:o["layoutMeta"].toArray())m_meta.append(v.toString());
    m_layout=o["layout"].toBool();m_regions=o["regions"].toArray().toVariantList();m_selected=-1;emit changed();
}
bool Document::load(const QJsonObject &o,QString *error) {
    const int w=o["width"].toInt(),h=o["height"].toInt();
    if(o["version"]!=1||w<256||w>4096||h<256||h>4096||qint64(w)*h>4194304||w%16||h%16||!o["regions"].isArray()||o["regions"].toArray().size()>100){
        if(error)*error="工程格式或画布尺寸无效";return false;
    }
    for(const auto &item:o["regions"].toArray()){
        auto r=item.toObject();double x=r["x"].toDouble(-1),y=r["y"].toDouble(-1),rw=r["width"].toDouble(),rh=r["height"].toDouble();
        if(!std::isfinite(x+y+rw+rh)||x<0||y<0||rw<4||rh<4||x+rw>w+0.01||y+rh>h+0.01||
            (r["type"]!="text"&&r["type"]!="object")){if(error)*error="工程包含无效或越界区域";return false;}
    }
    remember();restore(o);return true;
}
void Document::undo(){if(m_undo.isEmpty())return;m_redo.append(json());restore(m_undo.takeLast());}
bool Document::switchPage(const QJsonObject &object,QString *error){
    if(!load(object,error))return false;
    m_undo.clear();m_redo.clear();emit changed();return true;
}
void Document::redo(){if(m_redo.isEmpty())return;m_undo.append(json());restore(m_redo.takeLast());}
QString Document::validationError() const {
    if(m_prompt.trimmed().isEmpty())return "请先描述想要生成的画面";
    if(m_layout&&m_regions.isEmpty())return "请在画布上画出至少一个区域";
    for(auto &v:m_regions)if(m_layout){
        auto r=v.toMap();if(r["caption"].toString().trimmed().isEmpty())return "请填写每个区域的画面描述";
        if(r["type"]=="text"&&r["text"].toString().trimmed().isEmpty())return "请填写文字区域中需要显示的文字";
    }
    return {};
}
QString Document::compiledPrompt() const {
    const QString caption=m_prompt+(m_style.isEmpty()?QString():"\nVisual style: "+m_style);
    if(!m_layout)return caption;
    QJsonArray items;
    for(auto &v:m_regions){
        auto r=v.toMap();double x=r["x"].toDouble(),y=r["y"].toDouble();
        QJsonObject item{{"label",r["type"].toString()},{"caption",r["caption"].toString()},
            {"bbox",QJsonArray{int(std::floor(y))-m_height/2,int(std::floor(x))-m_width/2,
                int(std::ceil(y+r["height"].toDouble()))-m_height/2,int(std::ceil(x+r["width"].toDouble()))-m_width/2}}};
        if(r["type"]=="text")item["text"]=r["text"].toString();
        items.append(item);
    }
    QJsonObject summary{{"caption",caption},{"canvas","single"}};
    if(!m_meta.isEmpty())summary["meta"]=QJsonArray::fromStringList(m_meta);
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"summary",summary},
        {"items",items},{"bbox_coord_space","abs_centralized"},{"bbox_format","yxyx"}}).toJson(QJsonDocument::Compact));
}
void Document::example() {
    remember();m_width=1024;m_height=1024;m_layout=true;m_regions.clear();
    m_negative.clear();m_style.clear();m_meta.clear();
    m_prompt="A clean studio photograph of a blue toy robot on the left, a small orange cube in the center and a green potted plant on the right, all on a white table. Soft daylight, pale cream background.";
    const auto add=[this](QString name,QString caption,QString color,int x,int y,int w,int h){
        m_regions.append(QVariantMap{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"type","object"},{"name",name},
            {"caption",caption},{"text",""},{"color",color},{"x",x},{"y",y},{"width",w},{"height",h}});
    };
    add("蓝色机器人","A friendly blue toy robot, standing upright.","#5878E8",110,160,270,540);
    add("橙色方块","A small solid orange cube.","#E99B4B",450,540,180,190);
    add("绿植","A green leafy plant in a white ceramic pot.","#55A58A",700,230,220,470);
    add("桌子","A simple white table supporting the objects.","#9A91A5",60,670,900,280);
    m_selected=0;emit changed();
}

QString Document::importLayout(QString source,int w,int h){
    QJsonParseError parse;auto input=QJsonDocument::fromJson(source.toUtf8(),&parse);
    if(parse.error!=QJsonParseError::NoError||!input.isObject())return "JSON 格式错误："+parse.errorString();
    const auto o=input.object();const auto summary=o["summary"].toObject();
    if(o["bbox_format"]!="yxyx"||o["bbox_coord_space"]!="abs_centralized")
        return "需要 MOSS 的 abs_centralized / yxyx 坐标格式";
    if(!summary["caption"].isString()||!o["items"].isArray())return "缺少 summary.caption 或 items 数组";
    if(summary.contains("canvas")&&summary["canvas"]!="single")return "当前画布只支持 single 布局";
    if(summary.contains("meta")&&!summary["meta"].isArray())return "summary.meta 应为字符串数组";
    for(const auto &v:summary["meta"].toArray())if(!v.isString())return "summary.meta 应为字符串数组";
    QJsonArray regions;const QStringList colors={"#5878E8","#E99B4B","#55A58A","#A27BD2","#D97790"};
    for(const auto &v:o["items"].toArray()){
        if(!v.isObject())return "items 中包含无效区域";
        const auto item=v.toObject();const auto b=item["bbox"].toArray();const auto type=item["label"].toString();
        if(b.size()!=4||(type!="object"&&type!="text")||!item["caption"].isString())
            return "每个区域需要 object/text 类别、caption 和四个 bbox 坐标";
        for(const auto &n:b)if(!n.isDouble()||!std::isfinite(n.toDouble()))return "bbox 必须为有限数字";
        if(type=="text"&&!item["text"].isString())return "文字区域缺少 text 内容";
        regions.append(QJsonObject{{"id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"name",type=="text"?item["text"].toString().left(20):QString("对象 %1").arg(regions.size()+1)},
            {"type",type},{"caption",item["caption"]},{"text",item["text"].toString()},
            {"color",colors[regions.size()%colors.size()]},{"x",b[1].toDouble()+w/2.},{"y",b[0].toDouble()+h/2.},
            {"width",b[3].toDouble()-b[1].toDouble()},{"height",b[2].toDouble()-b[0].toDouble()}});
    }
    QJsonObject project{{"version",1},{"width",w},{"height",h},{"layout",true},
        {"prompt",summary["caption"]},{"layoutMeta",summary["meta"]},{"regions",regions}};
    QString error;if(!load(project,&error))return error;m_selected=m_regions.isEmpty()?-1:0;emit selectionChanged();return {};
}
void Document::loadTemplate(QString name){
    if(name=="objects"){example();return;}
    auto region=[](QString label,QString caption,QString text,QJsonArray box){
        QJsonObject r{{"label",label},{"caption",caption},{"bbox",box}};
        if(label=="text")r["text"]=text;return r;
    };
    QJsonArray items;QString caption;int w=1024,h=1280;
    if(name=="poster"){
        caption="一张留白充足的中文展览海报，奶油白背景，深蓝色标题，中央是一朵精致的蓝色纸艺花，整体清爽、优雅。文字逐字按指定内容绘制。";
        items={region("text","深蓝色粗体中文标题，居中，按照原文换行。","让灵感\n自由生长",{-550,-420,-270,420}),
               region("object","一朵精致的蓝色纸艺花，柔和阴影，细腻纸张纹理。","",{-170,-310,320,310}),
               region("text","底部小号深灰色无衬线字，居中。","创意生活展 · 2026",{410,-420,530,420})};
    }else if(name=="note"){
        w=h=1024;caption="A close-up photograph of a cream paper note on a warm wooden desk, natural daylight. Render the Chinese handwriting exactly as supplied, with the same line breaks.";
        items={region("text","Clear dark handwritten Chinese characters on the note, left aligned, one supplied line per line.","记得买牛奶和鸡蛋！\n牛奶两盒，鸡蛋一打。\n顺便带一袋面包。",{-300,-380,270,380})};
    }else return;
    importLayout(QString::fromUtf8(QJsonDocument(QJsonObject{{"summary",QJsonObject{{"caption",caption},{"canvas","single"},{"meta",QJsonArray{name=="poster"?"poster":"photograph"}}}},
        {"items",items},{"bbox_coord_space","abs_centralized"},{"bbox_format","yxyx"}}).toJson()),w,h);
}
