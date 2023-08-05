
#include <QSettings>
#include <QJsonValue>
#include <qjsondocument.h>
#include "Parsers/CompletionWords.h"
#include "Misc/Utility.h"
#include "Misc/SettingsStoreExtend.h"


const QStringList HTML_GLOBAL_ATTR = { "accesskey", "class", "contenteditableNew", "contextmenuNew", "data-", "dir", "draggableNew", "dropzoneNew", "hiddenNew", "id", "lang", "spellcheckNew", "style", "tabindex", "title", "translate" };

QMap<QString, QStringList> HTML_TAG_ATTR_MAP { 
	{"a",{"download","href","hreflang","media","rel","target","type"}},
	{"abbr" , {}},
	{"address" , {}},
	{"area" , {"alt","coords","href","hreflang","media","rel","shape","target","type"}},
	{"article" , {}},
	{"aside" , {}},
	{"audio" , {"autoplay","controls","loop","muted","preload","src"}},
	{"b" , {}},
	{"base" , {"href","target"}},
	{"bdi" , {}},
	{"bdo" , {"dir"}},
	{"blockquote" , {"cite"}},
	{"body" , {"alink","background","bgcolor","link","text","vlink"}},
	{"br" , {}},
	{"button" , {"autofocusNew","disabled","formNew","formactionNew","formenctypeNew","formmethodNew","formnovalidateNew","formtargetNew","name","type","value"}},
	{"canvas" , {"height","width"}},
	{"caption" , {}},
	{"cite" , {}},
	{"code" , {}},
	{"col" , {"span"}},
	{"colgroup" , {"span"}},
	{"datalist" , {}},
	{"dd" , {}},
	{"del" , {"cite","datetime"}},
	{"details" , {"open"}},
	{"dfn" , {}},
	{"dialog" , {}},
	{"div" , {}},
	{"dl" , {}},
	{"dt" , {}},
	{"em" , {}},
	{"embed" , {"heightNew","srcNew","typeNew","widthNew"}},
	{"fieldset" , {"disabled","form","name"}},
	{"figcaption" , {}},
	{"figure" , {}},
	{"footer" , {}},
	{"form" , {"accept-charset","action","autocompleteNew","enctype","method","name","novalidateNew","target"}},
	{"head" , {}},
	{"header" , {}},
	{"hgroup" , {}},
	{"h1" , {}},
	{"h2" , {}},
	{"h3" , {}},
	{"h4" , {}},
	{"h5" , {}},
	{"h6" , {}},
	{"hr" , {}},
	{"html" , {"manifest","xmlns"}},
	{"i" , {}},
	{"iframe" , {"height","name","sandboxNew","seamlessNew","src","srcdocNew","width"}},
	{"img" , {"loading","alt","crossoriginNew","height","ismap","src","usemap","width"}},
	{"input" , {"accept","alt","autocompleteNew","autofocusNew","checked","disabled","formNew","formactionNew","formenctypeNew","formmethodNew","formnovalidateNew","formtargetNew","heightNew","listNew","maxNew","maxlength","minNew","multipleNew","name","patternNew","placeholderNew","readonly","requiredNew","size","src","stepNew","type","value","widthNew"}},
	{"ins" , {"cite","datetime"}},
	{"kbd" , {}},
	{"label" , {"for","form"}},
	{"legend" , {}},
	{"li" , {"value"}},
	{"link" , {"href","hreflang","media","rel","sizesNew","type"}},
	{"main" , {}},
	{"map" , {"name"}},
	{"mark" , {}},
	{"meta" , {"charsetNew","content","http-equiv","name"}},
	{"meter" , {"formNew","highNew","lowNew","maxNew","minNew","optimumNew","value"}},
	{"nav" , {}},
	{"noscript" , {}},
	{"object" , {"data","formNew","height","name","type","usemap","width"}},
	{"ol" , {"reversedNew","start","type"}},
	{"optgroup" , {"disabled","label"}},
	{"option" , {"disabled","label","selected","value"}},
	{"output" , {"forNew","formNew","name"}},
	{"p" , {}},
	{"param" , {"name","value"}},
	{"pre" , {}},
	{"picture" , {}},
	{"progress" , {"max","value"}},
	{"q" , {"cite"}},
	{"rp" , {}},
	{"rt" , {}},
	{"ruby" , {}},
	{"s" , {}},
	{"samp" , {}},
	{"script" , {"asyncNew","charset","defer","src","type"}},
	{"section" , {}},
	{"select" , {"autofocusNew","disabled","formNew","multiple","name","requiredNew","size"}},
	{"small" , {}},
	{"source" , {"mediaNew","srcNew","type","sizes","srcset"}},
	{"span" , {}},
	{"strong" , {}},
	{"style" , {"media","scoped","type"}},
	{"sub" , {}},
	{"summary" , {}},
	{"sup" , {}},
	{"table" , {}},
	{"tbody" , {}},
	{"td" , {"colspan","headers","rowspan","width"}},
	{"textarea" , {"autofocusNew","cols","disabled","formNew","maxlengthNew","name","placeholderNew","readonly","requiredNew","rows","wrap"}},
	{"tfoot" , {}},
	{"th" , {"colspan","headers","rowspan","scope"}},
	{"thead" , {}},
	{"time" , {"datetime","pubdate"}},
	{"template" , {}},
	{"title" , {}},
	{"tr" , {}},
	{"track" , {"defaultNew","kindNew","labelNew","srcNew","srclang"}},
	{"u" , {}},
	{"ul" , {}},
	{"var" , {}},
	{"video" , {"autoplayNew","controlsNew","heightNew","loopNew","mutedNew","posterNew","preloadNew","srcNew","width"}},
	{"wbr" , {}},
};

QMap<QString, QString> AT_KEYWORDS_MAP = {
	{"@import","@import"},
	{"@font-face" , "@font-face"},
	{"@charset" , "@charset"},
	{"@counter-style" , "@counter-style"},
	{"@layer" , "@layer"},
	{"@keyframes" , "@keyframes"},
	{"@namespace" , "@namespace"},
	{"@page" , "@page"},
	{"@property" , "@property"},
	{"@supports" , "@supports"},
	{"@media" , "@media"},
};

const QStringList CSS_COLOR = { "rgb(0,0,0)","rgba(0,0,0,0.0)","hsl(0,0%,0%)","hsla(0,0%,0%,0.0)","aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black", "blanchedalmond", "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse", "chocolate", "coral", "cornflowerblue",
"cornsilk", "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", "darkgray", "darkgreen", "darkkhaki", "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon", "darkseagreen",
"darkslateblue", "darkslategray", "darkturquoise", "darkviolet", "deeppink", "deepskyblue", "dimgray", "dodgerblue", "firebrick", "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite",
"gold", "goldenrod", "gray", "green", "greenyellow", "honeydew", "hotpink", "indianred", "indigo", "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue", "lightcoral",
"lightcyan", "lightgoldenrodyellow", "lightgray", "lightgreen", "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray", "lightsteelblue", "lightyellow", "lime", "limegreen", "linen",
"magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise", "mediumvioletred", "midnightblue",
"mintcream", "mistyrose", "moccasin", "navajowhite", "navy", "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod", "palegreen", "paleturquoise", "palevioletred", "papayawhip",
"peachpuff", "peru", "pink", "plum", "powderblue", "purple", "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna", "silver", "skyblue", "slateblue",
"slategray", "snow", "springgreen", "steelblue", "tan", "teal", "thistle", "tomato", "turquoise", "violet", "wheat", "white", "whitesmoke", "yellow", "yellowgreen" };
const QStringList CSS_LENGTH = {
	"0cap","0ch","0cm","0cqb","0cqh","0cqi","0cqmax","0cqmin","0cqw","0dvb","0dvh","0dvi","0dvw","0em","0ex","0fr","0ic","0in","0ih","0ivb","0ivh","0ivi","0ivw",
	"0mm","0pc","0pt","0px","0q","0rcap","0rch","0rem","0rex","0ric","0rlh","0svb","0svh","0svi","0svw","0vb","0vh","0vi","0vw","0vmax","0vmin","calc()","var()"
};
const QStringList CSS_ATTR_WITH_COLOR = {
	"color","background","background-color","border-color","border-bottom-color","border-left-color","border-right-color","border-top-color","border-bottom","border-left","border-right","border-top","border","column-rule","column-rule-color","outline","outline-color"
};
const QStringList CSS_ATTR_WITH_LENGTH = {
	"background","background-size","border","border-width","border-bottom","border-left","border-right","border-top","border-bottom-width","border-left-width","border-right-width","border-top-width",
	"top","left","right","bottom","border-radius","border-bottom-left-radius","border-bottom-right-radius","border-top-left-radius","border-top-right-radius",
	"column-gap","column-rule","column-width","height","font","font-size","letter-spacing","line-height","margin","margin-top","margin-left","margin-right","margin-bottom",
	"max-height","max-width","min-height","min-width","outline","outline-offset","outline-width","padding","padding-top","padding-bottom","padding-left","padding-right",
	"perspective-origin","tab-size","text-indent","transform-origin","vertical-align","width","word-spacing","gap","row-gap",
};

QMap<QString, QStringList> CSS_ATTR_VALUE_MAP = {
	{"align-content",{"stretch","center","flex-start","flex-end","space-between","space-around","inherit","initial"}},
	{"align-items",{"stretch","center","flex-start","flex-end","baseline","inherit","initial"}},
	{"align-self",{"auto","stretch","center","flex-start","flex-end","baseline","inherit","initial"}},
	{"all",{"unset","inherit","initial"}},
	{"animation",{"animation-name","animation-duration","animation-timing-function","animation-delay","animation-iteration-count","animation-direction","animation-fill-mode","animation-play-state","inherit","initial"}},
	{"animation-delay",{"time"}},
	{"animation-direction",{"normal","reverse","alternate","alternate-reverse","initial","inherit"}},
	{"animation-duration",{"time"}},
	{"animation-fill-mode",{"none","forwards","backwards","both","inherit","initial"}},
	{"animation-iteration-count",{"number","infinite"}},
	{"animation-name",{"keyframename","none"}},
	{"animation-play-state",{"paused","running"}},
	{"animation-timing-function",{"linear","ease","ease-in","ease-out","ease-in-out","steps(int,start|end)","cubic-bezier(n,n,n,n)",}},
	{"appearance",{"normal","icon","window","button","menu","field"}},
	{"backface-visibility",{"visible","hidden"}},
	{"background",{"background-color","background-position","background-size","background-repeat","background-origin","background-clip","background-attachment","background-image"}},
	{"background-attachment",{"scroll","fixed","local","inherit","initial"}},
	{"background-blend-mode",{"normal","multiply","screen","overlay","darken","lighten","color-dodge","saturation","color","luminosity"}},
	{"background-clip",{"border-box","padding-box","content-box"}},
	{"background-color",{"transparent","inherit"}},
	{"background-image",{"url()","none","linear-gradient()","radial-gradient()","repeating-linear-gradient()","repeating-radial-gradient()","inherit"}},
	{"background-origin",{"padding-box","border-box","content-box"}},
	{"background-position", {"left top","left center","left bottom","right top","right center","right bottom","center top","center center","center bottom","x% y%","xpos ypos","inherit"}},
	{"background-repeat",{"repeat","repeat-x","repeat-y","no-repeat","inherit"}},
	{"background-size",{"percentage","cover","contain"}},
	{"border", {"thin","medium","thick","none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"border-bottom",{"thin","medium","thick","none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"border-bottom-color",{}},
	{"border-bottom-left-radius",{"0%"}},
	{"border-bottom-right-radius",{"0%"}},
	{"border-bottom-style",{"none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"border-bottom-width",{"thin","medium","thick","inherit"}},
	{"border-collapse",{"collapse","separate"}},
	{"border-color",{}},
	{"border-image",{}},
	{"border-image-outset",{"number"}},
	{"border-image-repeat",{"stretch","repeat","round","space","initial","inherit"}},
	{"border-image-slice",{"number","0%","fill"}},
	{"border-image-source",{"none","url()"}},
	{"border-image-width",{"number","0%","auto"}},
	{"border-left",{"thin","medium","thick","none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"border-left-color",{}},
	{"border-left-style",{"none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"border-left-width",{"thin","medium","thick","inherit"}},
	{"border-radius",{}},
	{"border-right",{"thin","medium","thick","none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"border-right-color",{}},
	{"border-right-style",{"none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"border-right-width",{"thin","medium","thick","inherit"}},
	{"border-spacing",{"length length","inherit"}},
	{"border-style",{"none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"border-top",{"thin","medium","thick","none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"border-top-color",{}},
	{"border-top-left-radius",{"0%"}},
	{"border-top-right-radius",{"0%"}},
	{"border-top-style",{"none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"border-top-width",{"thin","medium","thick","inherit"}},
	{"border-width",{"thin","medium","thick","inherit"}},
	{"bottom",{"auto","0%","inherit"}},
	{"box-shadow",{}},
	{"box-sizing",{"content-box","border-box","inherit"}},
	{"caption-side",{"top","bottom","inherit"}},
	{"clear",{"left","right","both","none","inherit"}},
	{"clip",{"shape","auto","inherit"}},
	{"color",{}},
	{"column-count",{"number","auto"}},
	{"column-fill",{"balance","auto"}},
	{"column-gap",{"normal"}},
	{"column-rule",{"none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset","thin","medium","thick"}},
	{"column-rule-color",{}},
	{"column-rule-style",{"none","hidden","dotted","dashed","solid","double","groove","ridge","inset","outset"}},
	{"column-rule-width",{"thin","medium","thick"}},
	{"column-span",{"1","all"}},
	{"column-width",{"auto"}},
	{"columns",{}},
	{"content",{"none","normal","counter","attr()","string","open-quote","close-quote","no-open-quote","no-close-quote","url()","inherit"}},
	{"counter-increment",{"none","id number","inherit"}},
	{"counter-reset",{"none","id number","inherit"}},
	{"cursor",{"url()","default","auto","crosshair","pointer","move","e-resize","ne-resize","nw-resize","n-resize","se-resize","sw-resize","s-resize","w-resize","text","wait","help"}},
	{"direction",{"ltr","rtl","inherit"}},
	{"display",{"none","block","inline","inline-block","list-item","run-in","compact","marker","table","inline-table","table-row-group","table-header-group","table-footer-group","flow-root","table-row","table-column-group","table-column","table-cell","table-caption","inherit"}},
	{"empty-cells",{"hide","show","inherit"}},
	{"filter",{"none","blur(px)","brightness(%)","contrast(%)","drop-shadow(h v blur spread color)","grayscale(%)","hue-rotate(deg)","invert(%)","opacity(%)","saturate(%)","sepia(%)","url()","initial","inherit"}},
	{"flex",{"flex-grow","flex-shrink","flex-basis","auto","none","initial","inherit"}},
	{"flex-basis",{"number","auto","initial","inherit"}},
	{"flex-direction",{"row","row-reverse","column","column-reverse","initial","inherit"}},
	{"flex-flow",{"flex-direction","flex-wrap","initial","inherit"}},
	{"flex-grow",{"number","initial","inherit"}},
	{"flex-shrink",{"number","initial","inherit"}},
	{"flex-wrap",{"nowrap","wrap","wrap-reverse","initial","inherit"}},
	{"float",{"left","right","none","inherit"}},
	{"font",{"italic","oblique","inherit","xx-small","x-small","small","medium","large","x-large","xx-large","smaller","larger","0%","small-caps","bold","bolder","lighter","100","200","300","400","500","600","700","800","900","caption","icon","menu","message-box","small-caption","status-bar","inherit"}},
	{"font-family",{"inherit"}},
	{"font-size",{"normal","xx-small","x-small","small","medium","large","x-large","xx-large","smaller","larger","0%","inherit"}},
	{"font-size-adjust",{}},
	{"font-stretch",{}},
	{"font-style",{"normal","italic","oblique","inherit"}},
	{"font-variant",{"normal","small-caps","inherit"}},
	{"font-weight",{"normal","bold","bolder","lighter","100","200","300","400","500","600","700","800","900","inherit"}},
	{"height",{"auto","0%","inherit"}},
	{"justify-content",{"flex-start","flex-end","center","space-between","space-evenly","space-around","initial","inherit"}},
	{"left",{"auto","0%","inherit"}},
	{"letter-spacing",{"normal","inherit"}},
	{"line-height",{"normal","number","0%","inherit"}},
	{"list-style",{"none","disc","circle","square","decimal","decimal-leading-zero","lower-roman","upper-roman","lower-alpha","upper-alpha","lower-greek","lower-latin","upper-latin","hebrew","armenian","georgian","cjk-ideographic","hiragana","katakana","hiragana-iroha","katakana-iroha","inside","outside","url()","initial","inherit"}},
	{"list-style-image",{"url()","none","inherit"}},
	{"list-style-position",{"inside","outside","inherit"}},
	{"list-style-type",{"none","disc","circle","square","decimal","decimal-leading-zero","lower-roman","upper-roman","lower-alpha","upper-alpha","lower-greek","lower-latin","upper-latin","hebrew","armenian","georgian","cjk-ideographic","hiragana","katakana","hiragana-iroha","katakana-iroha"}},
	{"margin",{"auto","0%","inherit"}},
	{"margin-bottom",{"auto","0%","inherit"}},
	{"margin-left",{"auto","0%","inherit"}},
	{"margin-right",{"auto","0%","inherit"}},
	{"margin-top",{"auto","0%","inherit"}},
	{"max-height",{"none","0%","inherit"}},
	{"max-width",{"none","0%","inherit"}},
	{"min-height",{"0%","inherit"}},
	{"min-width",{"0%","inherit"}},
	{"mix-blend-mode",{"normal","multiply","screen","overlay","darken","lighten","color-dodge","color-burn","hard-light","soft-light","difference","exclusion","hue","saturation","color","luminosity"}},
	{"object-fit",{"fill","contain","cover","none","scale-down","initial","inherit"}},
	{"object-position",{}},
	{"opacity",{"1.0","inherit"}},
	{"order",{"number","initial","inherit"}},
	{"outline",{"invert","none","dotted","dashed","solid","double","groove","ridge","inset","outset","thin","medium","thick","length","inherit"}},
	{"outline-color",{"invert","inherit"}},
	{"outline-offset",{"inherit"}},
	{"outline-style",{"none","dotted","dashed","solid","double","groove","ridge","inset","outset","inherit"}},
	{"outline-width",{"thin","medium","thick","inherit"}},
	{"overflow",{"visible","hidden","scroll","auto","inherit"}},
	{"overflow-x",{"visible","hidden","scroll","auto","no-display","no-content"}},
	{"overflow-y",{"visible","hidden","scroll","auto","no-display","no-content"}},
	{"padding",{"0%","inherit"}},
	{"padding-bottom",{"0%","inherit"}},
	{"padding-left",{"0%","inherit"}},
	{"padding-right",{"0%","inherit"}},
	{"padding-top",{"0%","inherit"}},
	{"page-break-after",{"auto","always","avoid","left","right","inherit"}},
	{"page-break-before",{"auto","always","avoid","left","right","inherit"}},
	{"page-break-inside",{"auto","avoid","inherit"}},
	{"perspective",{"number","none"}},
	{"perspective-origin",{"left","center","right","0%"}},
	{"position",{"absolute","fixed","relative","static","sticky","inherit","initial"}},
	{"quotes",{"none","string string string string","inherit"}},
	{"resize",{"none","both","horizontal","vertical"}},
	{"right",{"auto","0%","inherit"}},
	{"tab-size",{"number","initial","inherit"}},
	{"table-layout",{"automatic","fixed","inherit"}},
	{"text-align",{"left","right","center","justify","inherit"}},
	{"text-align-last",{"auto","left","right","center","justify","start","end","initial","inherit"}},
	{"text-decoration",{"none","underline","overline","line-through","blink","inherit"}},
	{"text-decoration-style",{"solid","double","dotted","dashed","wavy","initial","inherit"}},
	{"text-indent",{"0%","inherit"}},
	{"text-justify",{"auto","none","inter-word","inter-ideograph","inter-cluster","distribute","kashida"}},
	{"text-overflow",{"clip","ellipsis","string","initial","inherit"}},
	{"text-shadow",{}},
	{"text-transform",{"none","capitalize","uppercase","lowercase","inherit"}},
	{"top",{"auto","0%","inherit"}},
	{"transform",{"none","matrix(n,n,n,n,n,n)","matrix3d(n,n,n,n,n,n,n,n,n,n,n,n,n,n,n,n)","translate(x,y)","translate3d(x,y,z)","translateX(x)","translateY(y)","translateZ(z)","scale(x[,y]?)","scale3d(x,y,z)","scaleX(x)","scaleY(y)","scaleZ(z)","rotate(angle)","rotate3d(x,y,z,angle)","rotateX(angle)","rotateY(angle)","rotateZ(angle)","skew(x-angle,y-angle)","skewX(angle)","skewY(angle)","perspective(n)"}},
	{"transform-origin",{"left","center","right","0%"}},
	{"transform-style",{"flat","preserve-3d"}},
	{"transition",{"time","none","all","property","linear","ease","ease-in","ease-out","ease-in-out","cubic-bezier(n,n,n,n)"}},
	{"transition-delay",{"time"}},
	{"transition-duration",{"time"}},
	{"transition-property",{"none","all","property"}},
	{"transition-timing-function",{"linear","ease","ease-in","ease-out","ease-in-out","cubic-bezier(n,n,n,n)"}},
	{"unicode-bidi",{"normal","embed","bidi-override","initial","inherit"}},
	{"vertical-align",{"baseline","sub","super","top","text-top","middle","bottom","text-bottom","0%","inherit"}},
	{"visibility",{"visible","hidden","collapse","inherit"}},
	{"white-space",{"normal","pre","nowrap","pre-wrap","pre-line","inherit"}},
	{"width",{"auto","0%","inherit"}},
	{"word-break",{"normal","break-all","keep-all"}},
	{"word-spacing",{"normal","inherit"}},
	{"word-wrap",{"normal","break-word"}},
	{"z-index",{"auto","number","inherit"}},
	{"writing-mode",{"horizontal-tb","vertical-rl","vertical-lr","sideways-rl","sideways-lr"}},
	{"gird",{}},
	{"gap",{"length length"}},
	{"row-gap",{"normal","initial","inherit"}},
	{"grid-area",{"auto","span n","row-line","column-line"}},
	{"grid-auto-columns",{}},
	{"grid-auto-rows",{}},
	{"grid-auto-flow",{}},
	{"grid-column",{}},
	{"grid-column-start",{"auto","span n","column-line"}},
	{"grid-column-end",{"auto","span n","column-line"}},
	{"grid-row-end",{"auto","row-line","span n"}},
	{"grid-row-start",{"auto","row-line"}},
	{"grid-template",{}},
	{"grid-template-areas",{}},
	{"grid-template-columns",{}},
	{"grid-template-rows",{}},
	{"grid-row",{}},
	{"clip-path",{"clip-source","basic-shape","margin-box","border-box","padding-box","content-box","fill-box","stroke-box","view-box","none","initial","inherit"}},
	{"pointer-events",{}},
	{"backdrop-filte",{}}
};

CompletionWords::CompletionWords() 
{
	initHtmlCompletionMap();
	initCssCompletionMap();
}
void CompletionWords::initHtmlCompletionMap()
{
	SettingsStoreExtend sse;
	QJsonObject json = sse.getHTMLCompleterWordsJson();
	if (json.count() == 0 || !json.contains("tag-attr-map")) {
		json = genDefaultHTMLJson();
	}
	QStringList global_attrs;
	if (json.contains("global-attributes")) {
		global_attrs = json["global-attributes"].toVariant().toStringList();
	}
	QJsonObject subJson = json["tag-attr-map"].toObject();
	
	for (QJsonObject::iterator i = subJson.begin(); i != subJson.end(); ++i) {
		QStringList values = i.value().toVariant().toStringList();
		values << global_attrs;
		htmlTagAttrMap[i.key()] = values;
	}
}
void CompletionWords::initCssCompletionMap() 
{
	SettingsStoreExtend sse;
	QJsonObject json = sse.getCSSCompleterWordsJson();
	QJsonObject subJson;
	if (json.count() == 0 || !json.contains("attr-value-map") || !json.contains("atKeyWord-map")) {
		json = genDefaultCSSJson();
	}
	QStringList web_colors,lengths;
	if (json.contains("color")) {
		web_colors = json.value("color").toVariant().toStringList();
	}
	if (json.contains("length")) {
		lengths = json.value("length").toVariant().toStringList();
	}
	subJson = json["attr-value-map"].toObject();
	for (QJsonObject::iterator i = subJson.begin(); i != subJson.end(); ++i) {
		QStringList values = i.value().toVariant().toStringList();
		cssAttrValueMap[i.key()] = values;
	}
	foreach(QString attr, CSS_ATTR_WITH_COLOR) {
		if (cssAttrValueMap.contains(attr)) {
			cssAttrValueMap[attr] << web_colors;
		}
	}
	foreach(QString attr, CSS_ATTR_WITH_LENGTH) {
		if (cssAttrValueMap.contains(attr)) {
			cssAttrValueMap[attr] << lengths;
		}
	}
	subJson = json["atKeyWord-map"].toObject();
	for (QJsonObject::iterator i = subJson.begin(); i != subJson.end(); ++i) {
		cssAtKeywordMap[i.key()] == i.value().toVariant().toString();
	}
}
QStringList CompletionWords::getHtmlTagList() {
	return htmlTagAttrMap.keys();
}
QStringList CompletionWords::getHtmlAttrListForTag(QString tag) {
	if (htmlTagAttrMap.contains(tag))
		return htmlTagAttrMap.value(tag);
	return QStringList({});
}
QStringList CompletionWords::getCssAttrList() {
	return cssAttrValueMap.keys();
}
QStringList CompletionWords::getCssValueListForAttr(QString attr) {
	if (cssAttrValueMap.contains(attr))
		return cssAttrValueMap.value(attr);
	return QStringList({});
}
QStringList CompletionWords::getCssAtKeywordList() {
	return cssAtKeywordMap.keys();
}
QJsonObject CompletionWords::genDefaultHTMLJson() {
	SettingsStoreExtend sse;
	QJsonObject json;
	QJsonObject subJson = QJsonObject();
	json["global-attributes"] = QJsonValue::fromVariant(QVariant(HTML_GLOBAL_ATTR));
	QMap<QString, QStringList>::iterator i;
	for (i = HTML_TAG_ATTR_MAP.begin(); i != HTML_TAG_ATTR_MAP.end(); ++i) {
		subJson[i.key()] = QJsonValue::fromVariant(QVariant(i.value()));
	}
	json["tag-attr-map"] = subJson;
	sse.setHTMLCompleterWordsJson(json);
	return json;
}
QJsonObject CompletionWords::genDefaultCSSJson() {
	SettingsStoreExtend sse;
	QJsonObject json;
	QJsonObject subJson;
	json["color"] = QJsonValue::fromVariant(QVariant(CSS_COLOR));
	json["length"] = QJsonValue::fromVariant(QVariant(CSS_LENGTH));
	subJson = QJsonObject();
	for (QMap<QString, QStringList>::iterator i = CSS_ATTR_VALUE_MAP.begin(); i != CSS_ATTR_VALUE_MAP.end(); ++i) {
		subJson[i.key()] = QJsonValue::fromVariant(QVariant(i.value()));
	}
	json["attr-value-map"] = subJson;

	subJson = QJsonObject();
	for (QMap<QString, QString>::iterator i = AT_KEYWORDS_MAP.begin(); i != AT_KEYWORDS_MAP.end(); ++i) {
		subJson[i.key()] = QJsonValue::fromVariant(QVariant(i.value()));
	}
	json["atKeyWord-map"] = subJson;
	sse.setCSSCompleterWordsJson(json);
	return json;
}