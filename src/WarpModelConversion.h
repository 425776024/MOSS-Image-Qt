#pragma once
#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <stdexcept>
#include <vector>

// Minimal ONNX protobuf wire rewrite. No tensor decoding, Python, or protobuf
// runtime is needed. The caller must verify both source and result identities.
namespace WarpModelConversion {
struct Field { int number, wire; qsizetype begin, end; QByteArrayView value; quint64 integer=0; };
inline quint64 varint(QByteArrayView data,qsizetype &at) {
    quint64 value=0;
    for(int shift=0;shift<70;shift+=7) {
        if(at>=data.size())throw std::runtime_error("Truncated ONNX varint");
        const auto b=quint8(data[at++]);
        if(shift==63&&(b&0xfe))throw std::runtime_error("ONNX varint overflow");
        value|=quint64(b&127)<<shift;if(!(b&128))return value;
    }
    throw std::runtime_error("Invalid ONNX varint");
}
inline QByteArray encode(quint64 value) {
    QByteArray out;do {auto b=quint8(value&127);value>>=7;out.append(char(b|(value?128:0)));}while(value);return out;
}
inline std::vector<Field> fields(QByteArrayView data) {
    std::vector<Field> result;qsizetype at=0;
    while(at<data.size()) {
        const auto begin=at;const auto key=varint(data,at);const auto wire=int(key&7),number=int(key>>3);
        if(number<=0||key>>3>0x1fffffff)throw std::runtime_error("Invalid ONNX field");
        quint64 integer=0;qsizetype size=0,start=at;
        if(wire==0){integer=varint(data,at);size=at-start;}
        else {
            if(wire==2){const auto length=varint(data,at);start=at;if(length>quint64(data.size()-at))throw std::runtime_error("Invalid ONNX length");size=qsizetype(length);}
            else if(wire==1)size=8;else if(wire==5)size=4;
            else throw std::runtime_error("Unsupported ONNX wire type");
            if(size>data.size()-at)throw std::runtime_error("Truncated ONNX field");at+=size;
        }
        result.push_back({number,wire,begin,at,data.sliced(start,size),integer});
    }
    return result;
}
inline QByteArrayView stringField(QByteArrayView data,int number) {
    for(const auto &f:fields(data))if(f.number==number&&f.wire==2)return f.value;
    return {};
}
enum class Message { Model, Graph, Node, Attribute, Opset };
inline QByteArray rewrite(QByteArrayView data,Message type,int &modes) {
    QByteArray out;out.reserve(data.size());
    const bool grid=type==Message::Node&&stringField(data,4)==QByteArrayView("GridSample");
    const bool mode=type==Message::Attribute&&stringField(data,1)==QByteArrayView("mode");
    const bool standard=type==Message::Opset&&stringField(data,1).isEmpty();
    for(const auto &f:fields(data)) {
        QByteArray changed;bool replace=false;
        if(f.wire==0&&((type==Message::Model&&f.number==1)||(standard&&f.number==2))) {
            out+=encode(quint64(f.number)<<3);out+=encode(type==Message::Model?9:20);continue;
        }
        if(f.wire==2) {
            if(type==Message::Model&&f.number==7){changed=rewrite(f.value,Message::Graph,modes);replace=true;}
            else if(type==Message::Model&&f.number==8){changed=rewrite(f.value,Message::Opset,modes);replace=true;}
            else if(type==Message::Graph&&f.number==1){changed=rewrite(f.value,Message::Node,modes);replace=true;}
            else if(grid&&f.number==5){changed=rewrite(f.value,Message::Attribute,modes);replace=true;}
            else if(mode&&f.number==4) {
                if(f.value==QByteArrayView("bilinear")){changed="linear";replace=true;++modes;}
                else if(f.value==QByteArrayView("bicubic")){changed="cubic";replace=true;++modes;}
            }
        }
        if(replace){out+=encode((quint64(f.number)<<3)|2);out+=encode(changed.size());out+=changed;}
        else out.append(data.data()+f.begin,f.end-f.begin);
    }
    return out;
}
inline QByteArray convert(QByteArrayView data) {
    int modes=0;auto out=rewrite(data,Message::Model,modes);
    if(modes!=2)throw std::runtime_error("Unexpected Warp GridSample layout");
    return out;
}
}
