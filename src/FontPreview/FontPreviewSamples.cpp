/************************************************************************
**
**  This file is part of Sigil.
**
**  Reading lines are public-domain originals in that language.
**  They are not translations of each other, and they are not UI strings.
**
*************************************************************************/

#include "FontPreview/FontPreviewSamples.h"

namespace {

QString U(const char *utf8)
{
    return QString::fromUtf8(utf8);
}

FontPreviewSample Make(FontPreviewProfile profile, const char *headline,
                       const QStringList &passages, const QStringList &attributions,
                       const char *characters, const char *punctuation)
{
    FontPreviewSample sample;
    sample.profile = profile;
    sample.headline = U(headline);
    sample.passages = passages;
    sample.attributions = attributions;
    sample.characterLine = U(characters);
    sample.punctuationLine = U(punctuation);
    return sample;
}

const FontPreviewSample &Simplified()
{
    static const FontPreviewSample sample = Make(
        FontPreviewProfile::SimplifiedChinese,
        "满本都写着两个字是“吃人”！",
        {
            U("我翻开历史一查，这历史没有年代，歪歪斜斜的每页上都写着“仁义道德”几个字。我横竖睡不着，仔细看了半夜，才从字缝里看出字来，满本都写着两个字是“吃人”！"),
            U("鲁镇的酒店的格局，是和别处不同的：都是当街一个曲尺形的大柜台，柜里面预备着热水，可以随时温酒。只有穿长衫的，才踱进店面隔壁的房子里，要酒要菜，慢慢地坐喝。孔乙己是站着喝酒而穿长衫的唯一的人。他身材很高大；青白脸色，皱纹间时常夹些伤痕；一部乱蓬蓬的花白的胡子。穿的虽然是长衫，可是又脏又破，似乎十多年没有补，也没有洗。"),
            U("不必说碧绿的菜畦，光滑的石井栏，高大的皂荚树，紫红的桑椹；也不必说鸣蝉在树叶里长吟，肥胖的黄蜂伏在菜花上，轻捷的叫天子（云雀）忽然从草间直窜向云霄里去了。"),
            U("子曰：「学而时习之，不亦说乎？有朋自远方来，不亦乐乎？人不知而不愠，不亦君子乎？」")
        },
        {
            U("鲁迅《狂人日记》"),
            U("鲁迅《孔乙己》"),
            U("鲁迅《从百草园到三味书屋》"),
            U("《论语·学而》")
        },
        "天地人山水日月风云花鸟书页门国汉语龙马爱学长发后东",
        "，。！？；：“”‘’（）《》【】—…· 0123456789");
    return sample;
}

const FontPreviewSample &Traditional()
{
    static const FontPreviewSample sample = Make(
        FontPreviewProfile::TraditionalChinese,
        "誰解其中味？",
        {
            U("滿紙荒唐言，一把辛酸淚。\n都云作者痴，誰解其中味？"),
            U("花謝花飛花滿天，紅消香斷有誰憐？"),
            U("話說天下大勢，分久必合，合久必分。"),
            U("我與父親不相見已二年餘了，我最不能忘記的是他的背影。"),
            U("床前明月光，疑是地上霜。舉頭望明月，低頭思故鄉。"),
            U("關關雎鳩，在河之洲。窈窕淑女，君子好逑。")
        },
        {
            U("曹雪芹《紅樓夢》"),
            U("曹雪芹《葬花吟》"),
            U("羅貫中《三國演義》"),
            U("朱自清《背影》"),
            U("李白《靜夜思》"),
            U("《詩經·關雎》")
        },
        "天地人山水日月風雲花鳥書頁門國漢語龍馬愛學長發後東",
        "，。！？；：「」『』（）《》【】—…· 0123456789");
    return sample;
}

const FontPreviewSample &Mixed()
{
    static const FontPreviewSample sample = Make(
        FontPreviewProfile::ChineseMixed,
        "汉 漢",
        {
            U("汉 漢    语 語    书 書    体 體\n"
              "国 國    龙 龍    门 門    风 風\n"
              "云 雲    学 學    长 長    后 後\n"
              "发 發    东 東    爱 愛    页 頁")
        },
        { U("简繁对照") },
        "",
        "，。！？；：“”「」 0123456789");
    return sample;
}

const FontPreviewSample &Japanese()
{
    static const FontPreviewSample sample = Make(
        FontPreviewProfile::Japanese,
        "吾輩は猫である。",
        {
            U("吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している。"),
            U("親譲りの無鉄砲で小供の時から損ばかりしている。小学校に居る時分学校の二階から飛び降りて一週間ほど腰を抜かした事がある。"),
            U("ある日の暮方の事である。一人の下人が、羅生門の下で雨やみを待っていた。広い門の下には、この男のほかに誰もいない。ただ、所々丹塗の剥げた、大きな円柱に、蟋蟀が一匹とまっている。"),
            U("二人の若い紳士が、すっかりイギリスの兵隊のかたちをして、ぴかぴかする鉄砲をかついで、白熊のような犬を二疋つれて、だいぶ山奥の、木の葉のかさかさしたとこを、こんなことを云いながら、あるいておりました。\n"
              "「ぜんたい、ここらの山は怪しからんね。鳥も獣も一疋も居やがらん。なんでも構わないから、早くタンタアーンと、やって見たいもんだなあ。」"),
            U("春はあけぼの。やうやう白くなりゆく山際、少し明かりて、紫だちたる雲の細くたなびきたる。")
        },
        {
            U("夏目漱石『吾輩は猫である』"),
            U("夏目漱石『坊っちゃん』"),
            U("芥川龍之介『羅生門』"),
            U("宮沢賢治『注文の多い料理店』"),
            U("清少納言『枕草子』")
        },
        "あいうえお かきくけこ さしすせそ たちつてと なにぬねの\n"
        "はひふへほ まみむめも やゆよ らりるれろ わをん\n"
        "がぎぐげご ざじずぜぞ だぢづでど ばびぶべぼ ぱぴぷぺぽ\n"
        "っ ゃ ゅ ょ ー\n"
        "アイウエオ カキクケコ サシスセソ タチツテト ナニヌネノ\n"
        "ハヒフヘホ マミムメモ ヤユヨ ラリルレロ ワヲン\n"
        "ガギグゲゴ ザジズゼゾ ダヂヅデド バビブベボ パピプペポ\n"
        "ッ ャ ュ ョ ー\n"
        "日本語 春夏秋冬 山川空海 本文編集 読書 小説",
        "、。！？「」『』（）［］【】…・〜 0123456789");
    return sample;
}

const FontPreviewSample &English()
{
    static const FontPreviewSample sample = Make(
        FontPreviewProfile::English,
        "It is a truth universally acknowledged",
        {
            U("It is a truth universally acknowledged, that a single man in possession of a good fortune, must be in want of a wife."),
            U("It was the best of times, it was the worst of times, it was the age of wisdom, it was the age of foolishness, it was the epoch of belief, it was the epoch of incredulity, it was the season of Light, it was the season of Darkness, it was the spring of hope, it was the winter of despair."),
            U("To be, or not to be, that is the question:\nWhether 'tis nobler in the mind to suffer\nThe slings and arrows of outrageous fortune,\nOr to take arms against a sea of troubles\nAnd by opposing end them."),
            U("My father's family name being Pirrip, and my Christian name Philip, my infant tongue could make of both names nothing longer or more explicit than Pip. So, I called myself Pip, and came to be called Pip."),
            U("'Twas brillig, and the slithy toves\nDid gyre and gimble in the wabe:\nAll mimsy were the borogoves,\nAnd the mome raths outgrabe.\n"
              "\"Beware the Jabberwock, my son!\nThe jaws that bite, the claws that catch!\nBeware the Jubjub bird, and shun\nThe frumious Bandersnatch!\""),
            U("Exit, pursued by a bear."),
            U("The quick brown fox jumps over the lazy dog.")
        },
        {
            U("Jane Austen, Pride and Prejudice"),
            U("Charles Dickens, A Tale of Two Cities"),
            U("William Shakespeare, Hamlet"),
            U("Charles Dickens, Great Expectations"),
            U("Lewis Carroll, Jabberwocky"),
            U("William Shakespeare, The Winter's Tale"),
            U("English pangram")
        },
        "abcdefghijklmnopqrstuvwxyz\nABCDEFGHIJKLMNOPQRSTUVWXYZ\nAVATAR WA To Yo fi fl ffi ffl",
        "0123456789\n.,:;!?'\"()[]{} / \\ @ # $ % & * + - = _ < >");
    return sample;
}

const FontPreviewSample &Generic()
{
    static const FontPreviewSample sample = Make(
        FontPreviewProfile::Generic,
        "Aa Bb Cc 0123",
        {
            U("Aa Bb Cc 0123"),
            U("汉 漢 字"),
            U("あ ア")
        },
        {},
        "",
        "");
    return sample;
}

}

const FontPreviewSample &FontPreviewSampleFor(FontPreviewProfile profile)
{
    switch (profile) {
        case FontPreviewProfile::SimplifiedChinese: return Simplified();
        case FontPreviewProfile::TraditionalChinese: return Traditional();
        case FontPreviewProfile::ChineseMixed: return Mixed();
        case FontPreviewProfile::Japanese: return Japanese();
        case FontPreviewProfile::English: return English();
        case FontPreviewProfile::Generic: return Generic();
    }
    return Generic();
}

QString FontPreviewSpecimenText(const FontPreviewSample &sample)
{
    QString text = sample.headline;
    text += QLatin1Char('\n');
    text += sample.passages.join(QLatin1Char('\n'));
    text += QLatin1Char('\n');
    text += sample.characterLine;
    text += QLatin1Char('\n');
    text += sample.punctuationLine;
    return text;
}
