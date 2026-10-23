.pragma library

// Original text presets. Selecting one fills editable fields; it does not generate audio.
var entries = [
    {
        "id": "gentle-female",
        "name": "温柔讲述",
        "nameEn": "Warm narrator",
        "tag": "温柔 · 女声",
        "tagEn": "Warm · Female",
        "description": "温暖柔和的成年女声，中低音，吐字清楚，语速舒缓自然，像面对面讲故事，避免夸张的播音腔。",
        "descriptionEn": "A warm, gentle adult female voice in a mid-low register. Clear articulation, relaxed pacing, intimate storytelling, without an exaggerated announcer style.",
        "text": "你好，今天想和你分享一个温暖的小故事。让我们放慢脚步，听见生活里那些值得珍藏的瞬间。",
        "textEn": "Hello. Let us slow down and share a warm little story about the moments that make everyday life special."
    },
    {
        "id": "steady-male",
        "name": "沉稳男声",
        "nameEn": "Steady male",
        "tag": "沉稳 · 男声",
        "tagEn": "Steady · Male",
        "description": "沉稳可靠的成年男声，中低音，声音厚实但不刻意压低，语速适中，表达清晰，有自然的停顿。",
        "descriptionEn": "A steady, reassuring adult male voice in a mid-low register. Rich without forcing a deep pitch, moderately paced, clear, with natural pauses.",
        "text": "欢迎来到这里。接下来，我会一步一步说明，让复杂的信息变得清晰，也让每一个选择更加从容。",
        "textEn": "Welcome. We will take this one step at a time, making complex ideas clear and each choice easier to understand."
    },
    {
        "id": "news",
        "name": "新闻播报",
        "nameEn": "News anchor",
        "tag": "清晰 · 播报",
        "tagEn": "Clear · News",
        "description": "清晰利落的成年女声，标准普通话，发音准确，节奏紧凑而不过快，语气客观平稳，适合新闻资讯播报。",
        "descriptionEn": "A clear, composed adult female news voice. Precise articulation, brisk but unhurried pacing, an objective tone, and clean sentence endings.",
        "text": "这里是今日资讯。我们关注城市的发展，也记录生活中的变化。接下来，一起了解今天值得关注的消息。",
        "textEn": "Here is today's update. Let us look at the changes in our cities, our communities, and the lives around us."
    },
    {
        "id": "documentary",
        "name": "纪录片旁白",
        "nameEn": "Documentary",
        "tag": "深沉 · 旁白",
        "tagEn": "Rich · Narration",
        "description": "富有质感的成熟男声，中低音，呼吸自然，语速缓慢，叙述克制，有细腻的层次与适度的留白，适合纪录片旁白。",
        "descriptionEn": "A textured, mature male narrator in a mid-low register. Natural breathing, measured pacing, restrained expression, and thoughtful pauses for a documentary.",
        "text": "晨光越过山脊，河流在大地上缓缓延伸。这里的每一片叶子，都在讲述时间留下的故事。",
        "textEn": "Morning light reaches over the ridge. Along the quiet river, every leaf carries a story shaped by time."
    },
    {
        "id": "teacher",
        "name": "课程讲解",
        "nameEn": "Course teacher",
        "tag": "亲切 · 教学",
        "tagEn": "Friendly · Teaching",
        "description": "亲切耐心的成年女声，音色明亮清楚，语速适中，重点词语稍作强调，句子之间留出理解的时间，适合课程讲解。",
        "descriptionEn": "A friendly, patient adult female teacher. Bright and clear, moderately paced, with gentle emphasis and time to absorb each idea.",
        "text": "大家好，今天我们一起学习一个新知识。先看一个简单的例子，再把问题拆成几个步骤，你会发现它并不难。",
        "textEn": "Hello everyone. Let us begin with a simple example, then explore this new idea together, one step at a time."
    },
    {
        "id": "bedtime",
        "name": "睡前故事",
        "nameEn": "Bedtime story",
        "tag": "柔和 · 故事",
        "tagEn": "Soft · Stories",
        "description": "柔和低缓的成年男声，亲切温暖，语速稍慢，轻声叙述但不是耳语，情绪平静，适合睡前故事。",
        "descriptionEn": "A soft, warm adult male storyteller. Slightly slow and gently spoken, without whispering, with a peaceful bedtime mood.",
        "text": "夜色慢慢落下，小镇的灯一盏盏亮起来。小熊盖好被子，准备听一段关于星星和远方的故事。",
        "textEn": "Night settles softly, and the town lights come on. A little bear gets ready for a story about the stars."
    },
    {
        "id": "product",
        "name": "商品介绍",
        "nameEn": "Product guide",
        "tag": "自然 · 介绍",
        "tagEn": "Natural · Products",
        "description": "自然可信的成年女声，清晰轻快，语速适中，有适度的热情，不夸张叫卖，像向朋友介绍实用产品。",
        "descriptionEn": "A natural, trustworthy adult female product guide. Clear and lively, moderately paced, warmly enthusiastic without shouting or overselling.",
        "text": "每天的小改变，也能让生活更加轻松。这件产品从细节出发，让使用更顺手，让陪伴更长久。",
        "textEn": "Small changes can make daily life easier. Thoughtful details make this product simple and comfortable to use."
    },
    {
        "id": "youth",
        "name": "青年解说",
        "nameEn": "Young explainer",
        "tag": "轻快 · 解说",
        "tagEn": "Lively · Explaining",
        "description": "清爽自然的青年男声，中音，表达轻松，节奏明快，带一点好奇心，适合知识解说和日常分享。",
        "descriptionEn": "A fresh, natural young adult male voice. Mid register, relaxed delivery, lively pacing, and a curious tone for everyday explanations.",
        "text": "你有没有想过，这个看起来普通的现象，背后其实藏着有趣的原理？今天，我们就用几分钟把它说清楚。",
        "textEn": "Have you wondered why this ordinary thing happens? Let us explore the interesting idea behind it together."
    },
    {
        "id": "host",
        "name": "活力主持",
        "nameEn": "Upbeat host",
        "tag": "明亮 · 主持",
        "tagEn": "Bright · Hosting",
        "description": "明亮有活力的成年女声，声音开阔，节奏鲜明，热情但不尖锐，重点清晰，适合活动主持与节目开场。",
        "descriptionEn": "A bright, energetic adult female host. Open and resonant, rhythmically clear, enthusiastic without becoming shrill, ideal for an event opening.",
        "text": "欢迎各位来到今天的活动！很高兴在这里与你相遇，让我们带着好奇与期待，一起开启这段精彩旅程。",
        "textEn": "Welcome to today's event. It is wonderful to have you here. Let us begin with curiosity, energy, and excitement."
    },
    {
        "id": "audiobook-female",
        "name": "有声书女声",
        "nameEn": "Audiobook female",
        "tag": "细腻 · 叙事",
        "tagEn": "Expressive · Stories",
        "description": "细腻自然的成年女声，中音，叙事流畅，情绪变化克制，停顿随语意变化，适合小说与散文朗读。",
        "descriptionEn": "A nuanced, natural adult female audiobook voice. Mid register, flowing narration, restrained emotion, and pauses that follow the meaning.",
        "text": "她推开窗，看见雨后的街道泛着微光。远处有人走来，脚步不紧不慢，像一个久别重逢的约定。",
        "textEn": "She opened the window onto a street glowing after the rain. A familiar figure approached slowly in the distance."
    },
    {
        "id": "audiobook-male",
        "name": "有声书男声",
        "nameEn": "Audiobook male",
        "tag": "醇厚 · 叙事",
        "tagEn": "Full · Stories",
        "description": "醇厚清晰的成年男声，语速适中，叙述自然，有轻微的情绪起伏，不做夸张的角色表演，适合长篇朗读。",
        "descriptionEn": "A full, clear adult male audiobook voice. Moderate pace, natural narration, and subtle emotional shifts without exaggerated character acting.",
        "text": "那封信一直放在抽屉里。多年以后，他再次读起熟悉的字句，才明白有些答案，需要时间慢慢揭晓。",
        "textEn": "The letter stayed in the drawer for years. Reading it again, he found an answer that only time could reveal."
    },
    {
        "id": "meditation",
        "name": "冥想引导",
        "nameEn": "Calm meditation",
        "tag": "舒缓 · 引导",
        "tagEn": "Calm · Guidance",
        "description": "平静舒缓的成年女声，中低音，声音柔和稳定，语速缓慢，停顿自然，不使用夸张气声，适合放松引导。",
        "descriptionEn": "A calm, soothing adult female voice in a mid-low register. Soft and steady, slowly paced, with natural pauses and no exaggerated breathiness.",
        "text": "找一个舒服的姿势坐下，让肩膀慢慢放松。轻轻吸气，再缓缓呼气，把注意力带回此刻。",
        "textEn": "Sit comfortably and let your shoulders relax. Breathe in gently, breathe out slowly, and return to this moment."
    },
    {
        "id": "technology",
        "name": "科技讲解",
        "nameEn": "Technology guide",
        "tag": "理性 · 科技",
        "tagEn": "Clear · Technology",
        "description": "清晰理性的成年男声，中音，语速适中，术语发音准确，语气自信而平和，适合科技产品和技术原理介绍。",
        "descriptionEn": "A clear, analytical adult male technology voice. Mid register, moderate pace, precise terminology, confident yet approachable.",
        "text": "技术的价值，是让复杂的事情变得简单。接下来，我们从三个关键细节，了解这项功能如何真正改善体验。",
        "textEn": "Technology is valuable when it makes things simple. Let us explore three details that improve the experience."
    },
    {
        "id": "corporate",
        "name": "企业宣传",
        "nameEn": "Company profile",
        "tag": "专业 · 品牌",
        "tagEn": "Professional · Brand",
        "description": "专业大方的成年女声，清楚稳健，语气积极有信心，节奏舒展，避免口号式喊读，适合品牌与企业介绍。",
        "descriptionEn": "A professional, poised adult female brand voice. Clear and steady, positive and confident, comfortably paced, without shouting slogans.",
        "text": "我们相信，持久的价值来自认真做好每一件小事。从一次倾听，到一次改进，始终与每一位伙伴共同成长。",
        "textEn": "Lasting value comes from doing small things well. We listen carefully, make thoughtful improvements, and grow together."
    },
    {
        "id": "travel",
        "name": "旅行分享",
        "nameEn": "Travel journal",
        "tag": "轻松 · 旅行",
        "tagEn": "Relaxed · Travel",
        "description": "轻松亲切的青年男声，声音自然，语速适中，带一点发现新事物的愉悦，像向朋友分享旅途见闻。",
        "descriptionEn": "A relaxed, friendly young adult male travel voice. Natural and moderately paced, with the quiet delight of discovering somewhere new.",
        "text": "沿着这条小路继续向前，街角是一家安静的咖啡馆。不必急着赶路，最美的风景，也许就在下一个转弯。",
        "textEn": "Follow this little road to a quiet café. There is no need to hurry. The next turn may be the best part of the journey."
    },
    {
        "id": "science",
        "name": "亲切科普",
        "nameEn": "Friendly science",
        "tag": "明快 · 科普",
        "tagEn": "Lively · Science",
        "description": "亲切明快的成年女声，发音清楚，语调有自然变化，充满好奇但不幼稚，用简单易懂的节奏讲解科学知识。",
        "descriptionEn": "A friendly, lively adult female science voice. Clear articulation, naturally varied intonation, curious without sounding childish, and easy to follow.",
        "text": "为什么天空会呈现蓝色？答案就藏在阳光和空气的相遇里。让我们从一束光开始，认识这个有趣的世界。",
        "textEn": "Why does the sky look blue? Let us follow a beam of sunlight to explore the answer, one simple step at a time."
    },
    {
        "id": "service",
        "name": "客服助手",
        "nameEn": "Helpful assistant",
        "tag": "耐心 · 服务",
        "tagEn": "Patient · Service",
        "description": "礼貌耐心的成年女声，清晰自然，语速适中，语气温和可靠，重点信息准确，不带机械式的起伏。",
        "descriptionEn": "A polite, patient adult female assistant. Clear and natural, moderately paced, warm and reliable, with precise information and no robotic cadence.",
        "text": "您好，很高兴为您提供帮助。请先确认页面上的信息，我会陪您一步一步完成操作，如有疑问也可以随时告诉我。",
        "textEn": "Hello, I am happy to help. Please check the information on the page, and I will guide you through the next steps."
    },
    {
        "id": "elder",
        "name": "长者讲述",
        "nameEn": "Reflective elder",
        "tag": "温厚 · 回忆",
        "tagEn": "Warm · Reflection",
        "description": "温厚自然的年长男声，声线略带岁月感，发音清楚，语速稍慢，情绪平和，像耐心分享生活经历。",
        "descriptionEn": "A warm, natural older adult male voice with a little texture. Clearly articulated, slightly slow, and reflective, like sharing a life story.",
        "text": "年轻的时候，总觉得日子过得很慢。后来才懂得，那些平常的相聚与问候，才是记忆里最珍贵的部分。",
        "textEn": "When I was young, the days seemed slow. Later I learned that ordinary meetings and kind words were worth keeping."
    },
    {
        "id": "trailer",
        "name": "预告旁白",
        "nameEn": "Trailer narrator",
        "tag": "有力 · 预告",
        "tagEn": "Resonant · Trailer",
        "description": "有力量的成熟男声，中低音，发音扎实，节奏分明，停顿形成期待感，情绪适度，不使用夸张吼叫。",
        "descriptionEn": "A resonant mature male trailer voice in a mid-low register. Firm articulation, deliberate rhythm, and pauses that build anticipation without shouting.",
        "text": "一次意外的相遇，改变了原本平静的生活。当熟悉的世界出现新的线索，他们决定勇敢迈出下一步。",
        "textEn": "An unexpected meeting changes a quiet life. As new clues appear, they choose to take the next step together."
    },
    {
        "id": "casual",
        "name": "日常聊天",
        "nameEn": "Everyday chat",
        "tag": "自然 · 对话",
        "tagEn": "Natural · Chat",
        "description": "自然放松的青年女声，中音，像日常面对面聊天，语速适中，语气真诚，有轻微的口语节奏，不刻意播音。",
        "descriptionEn": "A relaxed, natural young adult female conversational voice. Mid register, moderate pace, sincere delivery, and everyday spoken rhythm without an announcer style.",
        "text": "今天过得怎么样？忙碌的时候，也别忘了给自己留一点休息的时间。喝杯水，看看窗外，再继续慢慢向前。",
        "textEn": "How has your day been? Take a moment to rest, have some water, and look outside before you begin again."
    }
];
