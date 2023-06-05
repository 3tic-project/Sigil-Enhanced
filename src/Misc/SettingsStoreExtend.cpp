#include "Misc/SettingsStoreExtend.h"
#include "QtCore/QString"
#include "Misc/Utility.h"
#include "qtextcodec.h" // modified: XHTML Fomat Configure

/*------------------ modified: XHTML Fomat Configure ----------------------*/

SettingsStoreExtend::SettingsStoreExtend()
    : QSettings(Utility::DefinePrefsDir() + "/" + "sigil_extend.ini", QSettings::IniFormat)
{
    initXhtmlFormat();
}

QString SettingsStoreExtend::getXhtmlFormat() {
    if (!contains("user_preferences/xhtml_format")) {
        return m_defaultXhtmlFormat;
    }
    return value("user_preferences/xhtml_format").toString();
}

void SettingsStoreExtend::setXhtmlFormat(QString conf) {
    setValue("user_preferences/xhtml_format", conf);
}

void SettingsStoreExtend::initXhtmlFormat() {
    QTextCodec* codec = QTextCodec::codecForName("GBK"); // turn the GBK chars to unicode codec.
    m_defaultXhtmlFormat = "";
    m_defaultXhtmlFormat += codec->toUnicode("/* global settings */\n@indent 2;\n@css-fold false;\n\n/* block-level elements */\nhtml, body, p, di");
    m_defaultXhtmlFormat += codec->toUnicode("v, h1, h2, h3, h4, h5, h6, ol, ul, li, address, blockquote, dd, dl, fieldset, form, hr, nav, menu, p");
    m_defaultXhtmlFormat += codec->toUnicode("re, table, tr, td, th, article\n{ \n  opentag-br : 1 0;\n  closetag-br: 0 1;\n}\n\n/* head elements ");
    m_defaultXhtmlFormat += codec->toUnicode("*/\nhead, meta, link, title, style, script \n{\n  opentag-br : 1 0;\n  closetag-br: 0 1;\n}\n\n/* xm");
    m_defaultXhtmlFormat += codec->toUnicode("l header */\n?xml { \n  opentag-br: 0 1;\n}\n\n/* doctype */\n!DOCTYPE { \n  opentag-br  : 1 2;\n  a");
    m_defaultXhtmlFormat += codec->toUnicode("ttr-fm-resv: true;\n}\n\n/* xhtml element */\nhtml { \n  inner-ind-adj:-1;\n}\n\n/* comment */\n!-- ");
    m_defaultXhtmlFormat += codec->toUnicode("{\n  attr-fm-resv: true;\n}\n\n/* main */\nbody{ \n  opentag-br : 2 1;\n  closetag-br: 1 1;\n}\n\nh1");
    m_defaultXhtmlFormat += codec->toUnicode(",h2,h3,h4,h5,h6 { \n  opentag-br : 2 0;\n  closetag-br: 0 2;\n}\npre {\n  text-fm-resv: true;\n}\n\n");
    m_defaultXhtmlFormat += codec->toUnicode("/* 配置说明：\n1. HTML代码格式化的配置语言类似CSS，通过选择器筛选具体节点，并由特定的属性指定该类节点的前后换行符数量和缩进级别。(仅)支持元素选择器、通配符选择器，除此以外的选择器类型");
    m_defaultXhtmlFormat += codec->toUnicode("都不支持。\n    选择器写法示例：\n    div { ... }\n    div * p { ... }\n    h1,h2,h3 { ... }\n选择器仅支持通过“后代”(A B)，“");
    m_defaultXhtmlFormat += codec->toUnicode("并集”(A,B)两种方式进行组合。\n需要注意的是，“后代选择器”的空格后面衔接的【只能是子代，不能隔代】，这点与标准CSS不同。\n\n2. 支持的属性包括：\n   opentag-br: 指定节");
    m_defaultXhtmlFormat += codec->toUnicode("点的【开始标签】前后的换行符个数。\n               值输入两个整数，格式为 【 opentag-br: n1 n2 】\n               n1 代表标签前面换行符个数，n");
    m_defaultXhtmlFormat += codec->toUnicode("2 代表标签后面换行符个数。\n               n1、n2 范围 0 ~ 9，默认值0。\n  closetag-br: 指定节点的【结束标签】前后的换行符个数。\n          ");
    m_defaultXhtmlFormat += codec->toUnicode("     范围 0 ~ 9，默认值 0。\n               格式同上。\n      ind-adj: 调整节点的缩进级别。\n               正数进级，负数退级，例如-2");
    m_defaultXhtmlFormat += codec->toUnicode("代表缩进退二级。\n               范围 -9 ~ 9，默认值 0。\ninner-ind-adj: 调整节点内部的缩进级别(不带上节点本身)。\n               范围 -");
    m_defaultXhtmlFormat += codec->toUnicode("9 ~ 9，默认值 0。\n attr-fm-resv: 指定开口标签内部是否保留非必需的空格和换行符。\n               范围 true 或 false，默认值 false。\n te");
    m_defaultXhtmlFormat += codec->toUnicode("xt-fm-resv: 指定节点内部文本是否保留非必需的空格和换行符。\n               范围 true 或 false，默认值 false。\n               启用该属性");
    m_defaultXhtmlFormat += codec->toUnicode("会让节点内部关于换行符和缩进的计算全部失效,\n               建议只给 pre 这类需要保留换行符和缩进的节点使用!\n    @css-fold: 指定Style节点的CSS是否折叠");
    m_defaultXhtmlFormat += codec->toUnicode("。\n               范围 true 或 false，默认值 false。\n               特殊属性，不需要写在花括号内部。\n      @indent: 指定每级缩进");
    m_defaultXhtmlFormat += codec->toUnicode("符的空格个数。\n               范围 0 ~ 4，默认值 2。\n               特殊属性，不需要写在花括号内部。\n注意：\na) 对于单标签（开口和闭合在同一标签，如");
    m_defaultXhtmlFormat += codec->toUnicode(" <img/>），开始标签和结束标签换行属性会叠加。\n     注释 <!-- comment --> 同样视为单标签，标签名为“!--”。\nb) 以上所有属性，除了 opentag-br 和 e");
    m_defaultXhtmlFormat += codec->toUnicode("ndtag-br 的值需要输入两个整数，其他属性的值均为一个整数。\n\n3. 换行符合并\n类似于相邻元素的上下margin会合并，在两个相邻节点之间，通过 opentag-br、closetag-");
    m_defaultXhtmlFormat += codec->toUnicode("br 指定的换行符数量取决于更大的那个，而非叠加。这种情况称为“换行符合并”。例如有CSS规则如下： \n    p { opentag-br: 2 0; closetag-br: 0 2; }\n那");
    m_defaultXhtmlFormat += codec->toUnicode("么两个相邻 p 元素之间的换行符数是 2，而非 2+2=4，因为节点之间换行符数量不会叠加。*/");
}