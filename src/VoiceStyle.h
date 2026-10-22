#pragma once
#include <QStringList>
#include <QVariantMap>

namespace VoiceStyle {
inline QStringList emotions(){return {"中性","高兴","悲伤","愤怒","恐惧","厌恶","惊讶"};}
inline QStringList dialects(){return {"standard","chuanyu","cantonese"};}
inline QVariantMap normalize(QVariantMap options){
    auto emotion=options.value("emotion","中性").toString();
    auto dialect=options.value("dialect","standard").toString();
    if(!emotions().contains(emotion))emotion="中性";
    if(!dialects().contains(dialect))dialect="standard";
    if(dialect!="standard")emotion="中性";
    return {{"emotion",emotion},{"dialect",dialect}};
}
}
