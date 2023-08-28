#include "Parsers/Emmet.h"
#include "Misc/SettingsStoreExtend.h"
#include "BookManipulation/CleanSource.h"
#include "Misc/Utility.h"

enum State {
    End         = 0,
    Undefined   = 1 << 0,
    Tag         = 1 << 1,
    ClassBegin  = 1 << 2,
    ClassVal    = 1 << 3,
    IdBegin     = 1 << 4,
    IdVal       = 1 << 5,
    AttrBegin   = 1 << 6,
    AttrEnd     = 1 << 7,
    AttrName    = 1 << 8,
    AttrVal     = 1 << 9,
    QuoteBegin  = 1 << 10,
    QuoteEnd    = 1 << 11,
    TextBegin   = 1 << 12,
    TextEnd     = 1 << 13,
    Assign      = 1 << 14,
    GroupBegin  = 1 << 15,
    GroupEnd    = 1 << 16,
    AddBrother  = 1 << 17,
    AddChild    = 1 << 18,
    AddParent   = 1 << 19,
    Copy        = 1 << 20,
    CopyNum     = 1 << 21,

};
enum CheckState {
    CkInvalid       = 1 << 0,
    CkUndefined     = 1 << 1,
    CkValied        = 1 << 2,
    CkNameStr       = 1 << 3,
    CkNameStrNum    = 1 << 4,
    CkUndefinedNum  = 1 << 5,
    CkMaybeAttr     = 1 << 6,
    CkMaybeText     = 1 << 7,
    CkQuote         = 1 << 8,
    CkQuoteEnd      = 1 << 9,
    CkProperty      = 1 << 10,
    CkConnect       = 1 << 11,
    CkCaret         = 1 << 12,
    CkMultiplier    = 1 << 13,
    CkNumSerieAt    = 1 << 14,
    CkAssign        = 1 << 15,
    CkGroupR        = 1 << 16,
    CkGroupL        = 1 << 17,

};
enum Token {
    OHTER,
    LETTER,
    NUM,
    LBRACE,
    RBRACE,
    LBRACKET,
    RBRACKET,
    LPAREN,
    RPAREN,
    HASHTAG,
    QUOTE,
    DOT,
    STAR,
    DOLLER,
    CARET,
    RANGLERACKET,
    PLUS,
    EQUAL,
    AT,
    WHITESPACE,
};

QHash<uint, QHash<ushort, uint>> CheckStateTable{
    {CheckState::CkUndefined , {
        {Token::LETTER    , CheckState::CkNameStr},
        {Token::NUM       , CheckState::CkUndefinedNum},
        {Token::RBRACE    , CheckState::CkMaybeText},
        {Token::RBRACKET  , CheckState::CkMaybeAttr},
        {Token::DOLLER    , CheckState::CkNameStrNum},
        }},
    {CheckState::CkUndefinedNum , {
        {Token::STAR      , CheckState::CkMultiplier},
        {Token::LETTER    , CheckState::CkNameStr},
        {Token::NUM       , CheckState::CkUndefinedNum},
        {Token::DOLLER    , CheckState::CkNameStrNum},
        {Token::AT        , CheckState::CkNumSerieAt},
        }},
    {CheckState::CkNameStr , {
        {Token::WHITESPACE    , CheckState::CkValied},
        {Token::LETTER        , CheckState::CkNameStr},
        {Token::NUM           , CheckState::CkNameStrNum},
        {Token::DOLLER        , CheckState::CkNameStrNum},
        {Token::AT            , CheckState::CkNumSerieAt},
        {Token::DOT           , CheckState::CkProperty},
        {Token::HASHTAG       , CheckState::CkProperty},
        {Token::PLUS          , CheckState::CkConnect},
        {Token::RANGLERACKET  , CheckState::CkConnect},
        {Token::CARET         , CheckState::CkCaret},
        {Token::LPAREN        , CheckState::CkGroupL},
        }},
    {CheckState::CkNameStrNum , {
        {Token::LETTER    , CheckState::CkNameStr},
        {Token::NUM       , CheckState::CkNameStrNum},
        {Token::DOT       , CheckState::CkProperty},
        {Token::HASHTAG   , CheckState::CkProperty},
        {Token::DOLLER    , CheckState::CkNameStrNum},
        {Token::AT        , CheckState::CkNumSerieAt},
        }},
    {CheckState::CkNumSerieAt , {
        {Token::DOLLER , CheckState::CkNameStrNum},
        }},
    {CheckState::CkProperty , {
        {Token::PLUS          , CheckState::CkConnect},
        {Token::RANGLERACKET  , CheckState::CkConnect},
        {Token::CARET         , CheckState::CkCaret},
        {Token::LETTER        , CheckState::CkNameStr},
        {Token::NUM           , CheckState::CkNameStrNum},
        {Token::DOLLER        , CheckState::CkNameStrNum},
        {Token::RBRACKET      , CheckState::CkMaybeAttr},
        {Token::RBRACE        , CheckState::CkMaybeText},
        {Token::LPAREN        , CheckState::CkGroupL},
        {Token::WHITESPACE    , CheckState::CkValied},
        }},
    {CheckState::CkConnect , {
        {Token::LETTER        , CheckState::CkNameStr},
        {Token::NUM           , CheckState::CkUndefinedNum},
        {Token::DOLLER        , CheckState::CkNameStrNum},
        {Token::RBRACKET      , CheckState::CkMaybeAttr},
        {Token::RBRACE        , CheckState::CkMaybeText},
        {Token::RPAREN        , CheckState::CkGroupR},
        }},
    {CheckState::CkCaret , {
        {Token::CARET         , CheckState::CkCaret},
        {Token::LETTER        , CheckState::CkNameStr},
        {Token::NUM           , CheckState::CkUndefinedNum},
        {Token::DOLLER        , CheckState::CkNameStrNum},
        {Token::RBRACKET      , CheckState::CkMaybeAttr},
        {Token::RBRACE        , CheckState::CkMaybeText},
        {Token::RPAREN        , CheckState::CkGroupR},
        }},
    {CheckState::CkMultiplier , {
        {Token::LETTER        , CheckState::CkNameStr},
        {Token::NUM           , CheckState::CkNameStrNum},
        {Token::DOLLER        , CheckState::CkNameStrNum},
        {Token::AT            , CheckState::CkNumSerieAt},
        {Token::RBRACKET      , CheckState::CkMaybeAttr},
        {Token::RBRACE        , CheckState::CkMaybeText},
        {Token::RPAREN        , CheckState::CkGroupR},
        }},
    {CheckState::CkGroupR , {
        {Token::RPAREN        , CheckState::CkGroupR},
        {Token::LETTER        , CheckState::CkNameStr},
        {Token::NUM           , CheckState::CkUndefinedNum},
        {Token::DOLLER        , CheckState::CkNameStrNum},
        {Token::RBRACKET      , CheckState::CkMaybeAttr},
        {Token::RBRACE        , CheckState::CkMaybeText},
        }},
    {CheckState::CkGroupL , {
        {Token::LPAREN        , CheckState::CkGroupL},
        {Token::PLUS          , CheckState::CkConnect},
        {Token::RANGLERACKET  , CheckState::CkConnect},
        {Token::CARET         , CheckState::CkCaret},
        {Token::WHITESPACE    , CheckState::CkValied},
        }},
    {CheckState::CkMaybeAttr , {
        {Token::LETTER        , CheckState::CkMaybeAttr | CheckState::CkNameStr},
        {Token::NUM           , CheckState::CkMaybeAttr | CheckState::CkNameStrNum},
        {Token::DOLLER        , CheckState::CkMaybeAttr | CheckState::CkNameStrNum},
        {Token::WHITESPACE    , CheckState::CkMaybeAttr},
        {Token::QUOTE         , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::LBRACKET      , CheckState::CkProperty},
        }},
    {CheckState::CkMaybeAttr | CheckState::CkQuote , {
        {Token::OHTER         , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::LETTER        , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::NUM           , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::LBRACE        , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::RBRACE        , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::LBRACKET      , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::RBRACKET      , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::LPAREN        , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::RPAREN        , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::HASHTAG       , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::QUOTE         , CheckState::CkMaybeAttr | CheckState::CkQuoteEnd},
        {Token::DOT           , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::STAR          , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::DOLLER        , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::CARET         , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::RANGLERACKET  , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::PLUS          , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::EQUAL         , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::AT            , CheckState::CkMaybeAttr | CheckState::CkQuote},
        {Token::WHITESPACE    , CheckState::CkMaybeAttr | CheckState::CkQuote},
        }},
    {CheckState::CkMaybeAttr | CheckState::CkNameStr , {
        {Token::WHITESPACE    , CheckState::CkMaybeAttr},
        {Token::NUM           , CheckState::CkMaybeAttr | CheckState::CkNameStrNum},
        {Token::LETTER        , CheckState::CkMaybeAttr | CheckState::CkNameStr},
        {Token::EQUAL         , CheckState::CkMaybeAttr | CheckState::CkAssign},
        {Token::LBRACKET      , CheckState::CkProperty},
        {Token::DOLLER        , CheckState::CkMaybeAttr | CheckState::CkNameStrNum},
        }},
    {CheckState::CkMaybeAttr | CheckState::CkNameStrNum , {
        {Token::NUM       , CheckState::CkMaybeAttr | CheckState::CkNameStrNum},
        {Token::LETTER    , CheckState::CkMaybeAttr | CheckState::CkNameStr},
        {Token::EQUAL     , CheckState::CkMaybeAttr | CheckState::CkAssign},
        {Token::DOLLER    , CheckState::CkMaybeAttr | CheckState::CkNameStrNum},
        {Token::AT        , CheckState::CkMaybeAttr | CheckState::CkNumSerieAt},
        }},
    {CheckState::CkMaybeAttr | CheckState::CkNumSerieAt , {
        {Token::DOLLER    , CheckState::CkMaybeAttr | CheckState::CkNameStrNum},
        }},
    {CheckState::CkMaybeAttr | CheckState::CkQuoteEnd , {
        {Token::EQUAL     , CheckState::CkMaybeAttr | CheckState::CkAssign},
        }},
    {CheckState::CkMaybeAttr | CheckState::CkAssign , {
        {Token::NUM       , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStrNum},
        {Token::LETTER    , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStr},
        {Token::DOLLER    , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStrNum},
        {Token::AT        , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNumSerieAt},
        }},
    {CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStrNum , {
        {Token::LETTER        , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStr},
        {Token::NUM           , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStrNum},
        {Token::DOLLER        , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStrNum},
        {Token::AT            , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNumSerieAt},
        }},
    {CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNumSerieAt , {
        {Token::DOLLER        , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStrNum},
        }},
    {CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStr , {
        {Token::WHITESPACE    , CheckState::CkMaybeAttr},
        {Token::NUM           , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStrNum},
        {Token::LETTER        , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStr},
        {Token::DOLLER        , CheckState::CkMaybeAttr | CheckState::CkAssign | CheckState::CkNameStrNum},
        {Token::LBRACKET      , CheckState::CkProperty},
        }},
    {CheckState::CkMaybeText , {
        {Token::OHTER         , CheckState::CkMaybeText},
        {Token::LETTER        , CheckState::CkMaybeText},
        {Token::NUM           , CheckState::CkMaybeText},
        {Token::LBRACE        , CheckState::CkProperty},
        {Token::RBRACE        , CheckState::CkMaybeText},
        {Token::LBRACKET      , CheckState::CkMaybeText},
        {Token::RBRACKET      , CheckState::CkMaybeText},
        {Token::LPAREN        , CheckState::CkMaybeText},
        {Token::RPAREN        , CheckState::CkMaybeText},
        {Token::HASHTAG       , CheckState::CkMaybeText},
        {Token::QUOTE         , CheckState::CkMaybeText},
        {Token::DOT           , CheckState::CkMaybeText},
        {Token::STAR          , CheckState::CkMaybeText},
        {Token::DOLLER        , CheckState::CkMaybeText},
        {Token::CARET         , CheckState::CkMaybeText},
        {Token::RANGLERACKET  , CheckState::CkMaybeText},
        {Token::PLUS          , CheckState::CkMaybeText},
        {Token::EQUAL         , CheckState::CkMaybeText},
        {Token::AT            , CheckState::CkMaybeText},
        {Token::WHITESPACE    , CheckState::CkMaybeText},
        }},
};

QHash<uint, QHash<ushort, uint>> StateTable{
    {State::Undefined , {
        {Token::LETTER       , State::Tag},
        {Token::NUM          , State::Tag},
        {Token::DOLLER       , State::Tag},
        {Token::DOT          , State::ClassBegin},
        {Token::HASHTAG      , State::IdBegin},
        {Token::LBRACKET     , State::AttrBegin},
        {Token::LBRACE       , State::TextBegin},
        {Token::LPAREN       , State::GroupBegin},
        }},
    {State::GroupBegin , {
        {Token::LETTER       , State::Tag},
        {Token::NUM          , State::Tag},
        {Token::DOLLER       , State::Tag},
        {Token::DOT          , State::ClassBegin},
        {Token::HASHTAG      , State::IdBegin},
        {Token::LBRACKET     , State::AttrBegin},
        {Token::LBRACE       , State::TextBegin},
        {Token::LPAREN       , State::GroupBegin},
        {Token::RPAREN       , State::GroupEnd},
        }},
    {State::GroupEnd , {
        {Token::PLUS         , State::AddBrother},
        {Token::RANGLERACKET , State::AddChild},
        {Token::CARET        , State::AddParent},
        {Token::STAR         , State::Copy},
        {Token::RPAREN       , State::GroupEnd},
        {Token::WHITESPACE   , State::End},
        }},
    {State::Tag , {
        {Token::LETTER       , State::Tag},
        {Token::NUM          , State::Tag},
        {Token::DOLLER       , State::Tag},
        {Token::AT           , State::Tag},
        {Token::DOT          , State::ClassBegin},
        {Token::HASHTAG      , State::IdBegin},
        {Token::LBRACKET     , State::AttrBegin},
        {Token::LBRACE       , State::TextBegin},
        {Token::PLUS         , State::AddBrother},
        {Token::RANGLERACKET , State::AddChild},
        {Token::CARET        , State::AddParent},
        {Token::STAR         , State::Copy},
        {Token::WHITESPACE   , State::End},
        {Token::RPAREN       , State::GroupEnd},
        }},
    {State::ClassBegin , {
        {Token::LETTER       , State::ClassVal},
        {Token::NUM          , State::ClassVal},
        {Token::DOLLER       , State::ClassVal},
        {Token::AT           , State::ClassVal},
        }},
    {State::ClassVal , {
        {Token::LETTER       , State::ClassVal},
        {Token::NUM          , State::ClassVal},
        {Token::DOLLER       , State::ClassVal},
        {Token::AT           , State::ClassVal},
        {Token::DOT          , State::ClassBegin},
        {Token::HASHTAG      , State::IdBegin},
        {Token::LBRACKET     , State::AttrBegin},
        {Token::LBRACE       , State::TextBegin},
        {Token::PLUS         , State::AddBrother},
        {Token::RANGLERACKET , State::AddChild},
        {Token::CARET        , State::AddParent},
        {Token::STAR         , State::Copy},
        {Token::WHITESPACE   , State::End},
        {Token::RPAREN       , State::GroupEnd},
        }},
    {State::IdBegin , {
        {Token::LETTER       , State::IdVal},
        {Token::NUM          , State::IdVal},
        {Token::DOLLER       , State::IdVal},
        {Token::AT           , State::IdVal},
        }},
    {State::IdVal , {
        {Token::LETTER       , State::IdVal},
        {Token::NUM          , State::IdVal},
        {Token::DOLLER       , State::IdVal},
        {Token::AT           , State::IdVal},
        {Token::DOT          , State::ClassBegin},
        {Token::HASHTAG      , State::IdBegin},
        {Token::LBRACKET     , State::AttrBegin},
        {Token::LBRACE       , State::TextBegin},
        {Token::PLUS         , State::AddBrother},
        {Token::RANGLERACKET , State::AddChild},
        {Token::CARET        , State::AddParent},
        {Token::STAR         , State::Copy},
        {Token::WHITESPACE   , State::End},
        {Token::RPAREN       , State::GroupEnd},
        }},
    {State::TextBegin , {
        {Token::OHTER           , State::TextBegin},
        {Token::LETTER          , State::TextBegin},
        {Token::NUM             , State::TextBegin},
        {Token::LBRACE          , State::TextBegin},
        {Token::RBRACE          , State::TextEnd},
        {Token::LBRACKET        , State::TextBegin},
        {Token::RBRACKET        , State::TextBegin},
        {Token::LPAREN          , State::TextBegin},
        {Token::RPAREN          , State::TextBegin},
        {Token::HASHTAG         , State::TextBegin},
        {Token::QUOTE           , State::TextBegin},
        {Token::DOT             , State::TextBegin},
        {Token::STAR            , State::TextBegin},
        {Token::DOLLER          , State::TextBegin},
        {Token::CARET           , State::TextBegin},
        {Token::RANGLERACKET    , State::TextBegin},
        {Token::PLUS            , State::TextBegin},
        {Token::EQUAL           , State::TextBegin},
        {Token::AT              , State::TextBegin},
        {Token::WHITESPACE      , State::TextBegin},
        }},
    {State::TextEnd , {
        {Token::DOT          , State::ClassBegin},
        {Token::HASHTAG      , State::IdBegin},
        {Token::RBRACKET     , State::AttrBegin},
        {Token::RBRACE       , State::TextBegin},
        {Token::PLUS         , State::AddBrother},
        {Token::RANGLERACKET , State::AddChild},
        {Token::CARET        , State::AddParent},
        {Token::STAR         , State::Copy},
        {Token::WHITESPACE   , State::End},
        {Token::RPAREN       , State::GroupEnd},
        }},
    {State::AttrBegin , {
        {Token::LETTER       , State::AttrName},
        {Token::NUM          , State::AttrName},
        {Token::DOLLER       , State::AttrName},
        {Token::AT           , State::AttrName},
        {Token::RBRACKET     , State::AttrEnd},
        {Token::WHITESPACE   , State::AttrBegin},
        }},
    {State::AttrName , {
        {Token::LETTER       , State::AttrName},
        {Token::NUM          , State::AttrName},
        {Token::DOLLER       , State::AttrName},
        {Token::AT           , State::AttrName},
        {Token::EQUAL        , State::Assign},
        {Token::RBRACKET     , State::AttrEnd},
        {Token::WHITESPACE   , State::AttrBegin},
        }},
    {State::Assign , {
        {Token::LETTER       , State::AttrVal},
        {Token::NUM          , State::AttrVal},
        {Token::DOLLER       , State::AttrVal},
        {Token::QUOTE        , State::AttrBegin | State::QuoteBegin},
        }},
    {State::AttrVal , {
        {Token::LETTER       , State::AttrVal},
        {Token::NUM          , State::AttrVal},
        {Token::DOLLER       , State::AttrVal},
        {Token::AT           , State::AttrVal},
        {Token::RBRACKET     , State::AttrEnd},
        {Token::WHITESPACE   , State::AttrBegin},
        }},
    {State::AttrBegin | State::QuoteBegin , {
        {Token::OHTER           , State::AttrBegin | State::QuoteBegin},
        {Token::LETTER          , State::AttrBegin | State::QuoteBegin},
        {Token::NUM             , State::AttrBegin | State::QuoteBegin},
        {Token::LBRACE          , State::AttrBegin | State::QuoteBegin},
        {Token::RBRACE          , State::AttrBegin | State::QuoteBegin},
        {Token::LBRACKET        , State::AttrBegin | State::QuoteBegin},
        {Token::RBRACKET        , State::AttrBegin | State::QuoteBegin},
        {Token::LPAREN          , State::AttrBegin | State::QuoteBegin},
        {Token::RPAREN          , State::AttrBegin | State::QuoteBegin},
        {Token::HASHTAG         , State::AttrBegin | State::QuoteBegin},
        {Token::QUOTE           , State::AttrBegin | State::QuoteEnd},
        {Token::DOT             , State::AttrBegin | State::QuoteBegin},
        {Token::STAR            , State::AttrBegin | State::QuoteBegin},
        {Token::DOLLER          , State::AttrBegin | State::QuoteBegin},
        {Token::CARET           , State::AttrBegin | State::QuoteBegin},
        {Token::RANGLERACKET    , State::AttrBegin | State::QuoteBegin},
        {Token::PLUS            , State::AttrBegin | State::QuoteBegin},
        {Token::EQUAL           , State::AttrBegin | State::QuoteBegin},
        {Token::AT              , State::AttrBegin | State::QuoteBegin},
        {Token::WHITESPACE      , State::AttrBegin | State::QuoteBegin},
        }},
    {State::AttrBegin | State::QuoteEnd , {
        {Token::WHITESPACE   , State::AttrBegin},
        {Token::RBRACKET     , State::AttrEnd},
        }},
    {State::AttrEnd , {
        {Token::DOT          , State::ClassBegin},
        {Token::HASHTAG      , State::IdBegin},
        {Token::LBRACKET     , State::AttrBegin},
        {Token::LBRACE       , State::TextBegin},
        {Token::PLUS         , State::AddBrother},
        {Token::RANGLERACKET , State::AddChild},
        {Token::CARET        , State::AddParent},
        {Token::STAR         , State::Copy},
        {Token::WHITESPACE   , State::End},
        {Token::RPAREN       , State::GroupEnd},
        }},
    {State::AddBrother , {
        {Token::LETTER       , State::Tag},
        {Token::NUM          , State::Tag},
        {Token::DOLLER       , State::Tag},
        {Token::DOT          , State::ClassBegin},
        {Token::HASHTAG      , State::IdBegin},
        {Token::RBRACKET     , State::AttrBegin},
        {Token::RBRACE       , State::TextBegin},
        {Token::LPAREN       , State::GroupBegin},
        }},
    {State::AddChild , {
        {Token::LETTER       , State::Tag},
        {Token::NUM          , State::Tag},
        {Token::DOLLER       , State::Tag},
        {Token::DOT          , State::ClassBegin},
        {Token::HASHTAG      , State::IdBegin},
        {Token::RBRACKET     , State::AttrBegin},
        {Token::RBRACE       , State::TextBegin},
        {Token::LPAREN       , State::GroupBegin},
        }},
    {State::AddParent , {
        {Token::LETTER       , State::Tag},
        {Token::NUM          , State::Tag},
        {Token::DOLLER       , State::Tag},
        {Token::DOT          , State::ClassBegin},
        {Token::HASHTAG      , State::IdBegin},
        {Token::RBRACKET     , State::AttrBegin},
        {Token::RBRACE       , State::TextBegin},
        {Token::CARET        , State::AddParent},
        {Token::LPAREN       , State::GroupBegin},
        }},
    {State::Copy , {
        {Token::NUM          , State::CopyNum},
        }},
    {State::CopyNum , {
        {Token::NUM          , State::CopyNum},
        {Token::RBRACKET     , State::AttrBegin},
        {Token::RBRACE       , State::TextBegin},
        {Token::RANGLERACKET , State::AddChild},
        {Token::PLUS         , State::AddBrother},
        {Token::CARET        , State::AddParent},
        {Token::RPAREN       , State::GroupEnd},
        {Token::WHITESPACE   , State::End},
        }},
};

QStringList HTML_TAG_LIST = { "a","abbr","address","area","article","aside","audio","b","base","bdi","bdo","blockquote","body","br","button","canvas","caption",
              "cite","code","col","colgroup","datalist","dd","del","details","dfn","dialog","div","dl","dt","em","embed","fieldset","figcaption",
              "figure","footer","form","head","header","hgroup","h1","h2","h3","h4","h5","h6","hr","html","i","iframe","img","input","ins","kbd",
              "label","legend","li","link","main","map","mark","meta","meter","nav","noscript","object","ol","optgroup","option",
              "output","p","param","pre","picture","progress","q","rp","rt","ruby","s","samp","script","section","select","small","source","span",
              "strong","style","sub","summary","sup","table","tbody","td","textarea","tfoot","th","thead","time","template","title","tr","track",
              "u","ul","var","video","wbr" };

Emmet::Emmet(QString unverified_text)
    : abbrev(""),
    xmlFormatParser(SettingsStoreExtend().getXhtmlFormat())
{
}
QString Emmet::get_abbreviation()
{
    return abbrev;
}
void Emmet::set_abbreviation(QString unverified_text)
{
    abbrev = getValidAbbrev(unverified_text);
}
QString Emmet::get_parsedText()
{
    return parse_abbrev();
}

ushort Emmet::getTokenType(QChar ch)
{
    if (('a' <= ch && 'z' >= ch) || ('A' <= ch && 'Z' >= ch) || ch == '_' || ch == '-') {
        return Token::LETTER;
    }
    else if ('0' <= ch && '9' >= ch) {
        return Token::NUM;
    }
    else if (ch == '"' || ch == '\'') {
        if (last_quote.isNull()) {
            last_quote = ch;
            return Token::QUOTE;
        }
        else if (ch == last_quote) {
            last_quote = QChar();
            return Token::QUOTE;
        }
    }

    switch (ch.unicode()) {
    case 93: // ]
        return Token::RBRACKET;
    case 91: // [
        return Token::LBRACKET;
    case 125: // }
        return Token::RBRACE;
    case 123: // {
        return Token::LBRACE;
    case 41: // )
        return Token::RPAREN;
    case 40: // (
        return Token::LPAREN;
    case 42: // *
        return Token::STAR;
    case 35: // #
        return Token::HASHTAG;
    case 46: // .
        return Token::DOT;
    case 43: // +
        return Token::PLUS;
    case 62: // >
        return Token::RANGLERACKET;
    case 94: // ^
        return Token::CARET;
    case 36: // $
        return Token::DOLLER;
    case 64: // @
        return Token::AT;
    case 61: // =
        return Token::EQUAL;
    case 32: // " "
        return Token::WHITESPACE;
    case 10: // \n
        return Token::WHITESPACE;
    case 9: // \t
        return Token::WHITESPACE;
    default:
        return Token::OHTER;
    }

}
QString Emmet::getValidAbbrev(QString unverified_text)
{
    if (unverified_text.isEmpty())
        return QString();
    QString abbr;
    curent_indent = getCurrentIndent(unverified_text);
    unverified_text = Utility::RegExpSub("<[^>]*>", "", unverified_text);
    unverified_text.prepend(" ");
    short paren_offset = 0;
    uint Len = unverified_text.size(),
        i = Len;
    uint state = CheckState::CkUndefined;
    uint all_states = 0;
    while (i > 0) {
        QChar pre_ch = unverified_text.at(i - 1);
        ushort token = getTokenType(pre_ch);
        if (token == Token::RPAREN) {
            ++paren_offset;
        }
        else if (token == Token::LPAREN) {
            --paren_offset;
        }
        if (CheckStateTable[state].contains(token)) {
            state = CheckStateTable[state][token];
        }
        else {
            state = CheckState::CkInvalid;
        }
        if (state == CheckState::CkInvalid || state == CheckState::CkValied) {
            break;
        }
        all_states = all_states | state;
        --i;
    }
    if (state & CheckState::CkInvalid) {
        return QString();
    }
    if (state & CheckState::CkValied) {
        abbr = unverified_text.mid(i, Len - i);
    }
    if (!abbr.isEmpty() && (all_states == CheckState::CkNameStr ||
                            all_states == (CheckState::CkNameStr|CheckState::CkUndefinedNum)))
    {
        if (!HTML_TAG_LIST.contains(abbr))
            abbr = QString();
    }
    if (!abbr.isEmpty() && (all_states & (CheckState::CkGroupL|CheckState::CkGroupR)) ) {
        if (paren_offset != 0)
            abbr = QString();
    }
    return abbr;
}
QString Emmet::parse_abbrev()
{
    QString abbr = abbrev + " ";
    uint state = State::Undefined,
        pre_state = state;
    ushort i = 0, pre_i = 0;
    Element* root = new Element();
    Element* temp_ele = new Element("p");
    ushort temp_eleCopyNum = 0;
    QHash<QString, QString> tempAttrDict;
    QList<QList<Element*>> parentStack = { {root} };
    QList<QList<Element*>>* parentStack_p = &parentStack;
    QList<QList<QList<Element*>>> groupStack;
    QString tempAttrName;

    while (i < abbr.size()) {
        QChar ch = abbr.at(i);
        ushort token = getTokenType(ch);

        if (StateTable[state].contains(token)) {
            state = StateTable[state][token];
        }
        else {
            state = State::End;
        }

        if (state == State::GroupBegin) {
            groupStack.append({ {new Element()} });
            parentStack_p = &groupStack.last();
        }

        if (pre_state != state) {
            switch (pre_state) {
            case State::Tag:
                temp_ele->tagName = abbr.mid(pre_i, i - pre_i);
                break;
            case State::ClassVal:
                temp_ele->classlist.append(abbr.mid(pre_i, i - pre_i));
                break;
            case State::IdVal:
                temp_ele->id = abbr.mid(pre_i, i - pre_i);
                break;
            case State::AttrName:
                tempAttrName = abbr.mid(pre_i, i - pre_i);
                if (tempAttrName == "id") {
                    temp_ele->id = "";
                }
                else if (tempAttrName == "class") {
                    temp_ele->classlist.append("");
                }
                else {
                    tempAttrDict[tempAttrName] = "";
                }
                break;
            case State::AttrVal:
            {
                QString tempAttrValue = abbr.mid(pre_i, i - pre_i);
                if (tempAttrName == "id") {
                    temp_ele->id = tempAttrValue;
                }
                else if (tempAttrName == "class") {
                    temp_ele->classlist.append(tempAttrValue);
                }
                else {
                    tempAttrDict[tempAttrName] = tempAttrValue;
                }
                break;
            }
            case State::AttrBegin | State::QuoteBegin:
            {
                QString tempAttrValue = abbr.mid(pre_i + 1, i - pre_i - 1);
                if (tempAttrName == "id") {
                    temp_ele->id = tempAttrValue;
                }
                else if (tempAttrName == "class") {
                    QStringList clslist = tempAttrValue.split(" ");
                    temp_ele->classlist += clslist;
                }
                else {
                    tempAttrDict[tempAttrName] = tempAttrValue;
                }
                break;
            }
            case State::CopyNum:
            {
                temp_eleCopyNum = abbr.mid(pre_i, i - pre_i).toInt();
                break;
            }

            }
            switch (state) {
            case State::AttrEnd:
                temp_ele->attrdict = tempAttrDict;
                break;
            case State::TextEnd:
                temp_ele->text = abbr.mid(pre_i + 1, i - pre_i - 1);
                break;
            case State::AddBrother:
            {
                QList<Element*> pnodes = parentStack_p->last();
                QList<Element*> temp_elements = multipleCopyElement(temp_ele, temp_eleCopyNum);
                foreach(Element * pnode, pnodes) {
                    pnode->children += temp_elements;
                }
                temp_ele = new Element("p");
                temp_eleCopyNum = 0;
                break;
            }
            case State::AddChild:
            {
                QList<Element*> pnodes = parentStack_p->last();
                QList<Element*> temp_elements = multipleCopyElement(temp_ele, temp_eleCopyNum);
                QList<Element*> temp_parents;
                foreach(Element * pnode, pnodes) {
                    foreach(Element * ele, temp_elements) {
                        Element* ele_ = copyElement(ele, 1);
                        pnode->children << ele_;
                        temp_parents << ele_;
                    }
                }
                parentStack_p->append(temp_parents);
                temp_ele = new Element("p");
                temp_eleCopyNum = 0;
                break;
            }
            case State::AddParent:
            {
                QList<Element*> pnodes = parentStack_p->last();
                QList<Element*> temp_elements = multipleCopyElement(temp_ele, temp_eleCopyNum);
                foreach(Element * pnode, pnodes) {
                    pnode->children += temp_elements;
                }
                if (parentStack_p->size() > 1)
                    parentStack_p->takeLast();
                temp_ele = new Element("p");
                temp_eleCopyNum = 0;
                break;
            }
            }
        }
        if (pre_state == state) {
            if (state == State::AddParent) {
                if (parentStack_p->size() > 1)
                    parentStack_p->takeLast();
            }
        }
        if (state == State::End || state == State::GroupEnd) {
            QList<Element*> pnodes = parentStack_p->last();
            QList<Element*> temp_elements = multipleCopyElement(temp_ele, temp_eleCopyNum);
            foreach(Element * pnode, pnodes) {
                pnode->children += temp_elements;
            }

            if (state == State::GroupEnd) {
                temp_ele = parentStack_p->first()[0];
                groupStack.takeLast();
                if (groupStack.size() > 0) {
                    parentStack_p = &groupStack.last();
                }
                else {
                    parentStack_p = &parentStack;
                }
            }
            if (state == State::End) {
                break;
            }
        }
        if (pre_state != state) {
            pre_i = i;
            pre_state = state;
        }
        ++i;
    }
    QString full_text = elementToString(root);
    full_text = formatCode(full_text);
    return full_text;
}
QString Emmet::convertDollarSymbols(QString text, short num)
{
    if (num < 1) {
        return text;
    }
    text += " ";
    uint Len = text.size();
    uint i = 0;
    QChar pre_ch;
    ushort dollar_count = 0;
    ushort order_start = 0;
    bool convert_ready = false;
    QString obtained_numstr = "";
    QString text_ = "";

    while (i < Len) {
        QChar ch = text.at(i);
        if (ch == '$') {
            if (pre_ch == '$') {
                ++dollar_count;
            }
            else {
                dollar_count = 1;
            }
        }
        if (dollar_count > 0) {
            if (ch == '@') {
                uint j = i + 1;
                while (j < Len && '0' <= text.at(j) && '9' >= text.at(j))
                    ++j;
                if (j > i + 1) {
                    obtained_numstr = text.mid(i + 1, j - i - 1);
                    order_start = obtained_numstr.toInt() - 1;
                    i = j - 1;
                }
                else {
                    order_start = 0;
                }
                convert_ready = true;
            }
            else if (ch != '$') {
                order_start = 0;
                convert_ready = true;
                --i;
            }
        }
        if (dollar_count == 0) {
            text_.append(ch);
        }
        if (convert_ready) {
            ushort zero_count = QString::number(order_start + num).size() < dollar_count ? dollar_count - QString::number(order_start + num).size() : 0;
            if (zero_count > 0) {
                text_ += QString(zero_count, '0') + QString::number(order_start + num);
            }
            else {
                text_ += QString::number(order_start + num);
            }
            convert_ready = false;
            obtained_numstr = QString();
            dollar_count = 0;
        }
        pre_ch = ch;
        ++i;
    }

    return text_.left(Len - 1);
}
QString Emmet::elementToString(Element* em) {
    QString text = "";
    bool closed = em->children.size() == 0 ? true : false;
    text += elementToTagString(em, closed);
    foreach(Element * child, em->children) {
        text += elementToString(child);
    }
    if (em->tagName != "" && em->children.size() > 0) {
        text += QString("</%1>").arg(em->tagName);
    }
    return text;
}
QString Emmet::elementToTagString(Element* em, bool closed) {
    if (em->tagName.isEmpty()) {
        return "";
    }
    QString tagstr;
    tagstr = QString("<%1").arg(em->tagName);
    if (!em->classlist.isEmpty()) {
        QStringList clslist = filter(em->classlist);
        tagstr += " class=\"";
        for (ushort i = 0; i < clslist.size(); ++i) {
            QString item = clslist.at(i);
            if (i < clslist.size() - 1) {
                tagstr += item + " ";
            }
            else if (i == clslist.size() - 1) {
                tagstr += item;
            }
        }
        if (clslist.isEmpty())
            tagstr += "\v";
        tagstr += "\"";
    }
    if (em->id != "\x12") {
        if (em->id.isEmpty()) {
            tagstr += " id=\"\v\"";
        }
        else {
            tagstr += QString(" id=\"%1\"").arg(em->id);
        }
    }
    QHash<QString, QString>::iterator i;
    for (i = em->attrdict.begin(); i != em->attrdict.end(); ++i) {
        QString key = i.key(),
            val = i.value();
        if (val == "")
            val = "\v";
        tagstr += QString(" %1=\"%2\"").arg(key).arg(val);
    }
    if (closed) {
        if (em->text.isEmpty() && QStringList({ "br","img","hr","link","meta","input","base" }).contains(em->tagName)) {
            tagstr += "/>";
        }
        else {
            if (em->text.isEmpty()) {
                tagstr += QString(">\v</%1>").arg(em->tagName);
            }
            else {
                tagstr += QString(">%1</%2>").arg(em->text).arg(em->tagName);
            }
        }
    }
    else {
        tagstr += ">" + em->text;
    }
    return tagstr;
}
QString Emmet::getCurrentIndent(QString line_text) {
    QChar ch = line_text.at(0);
    int i = Utility::StringTrimmedIndex(line_text).before;
    if (i > 0)
        return line_text.left(i);
    return "";
}
QString Emmet::formatCode(QString text) {
    text = CleanSource::PrettifyXhtml(text, xmlFormatParser);
    text = Utility::trimmed(text, "\n");
    if (!curent_indent.isEmpty()) {
        text.replace("\n", "\n" + curent_indent);
    }
    return text;
}
QStringList Emmet::filter(QStringList& _list)
{
    QStringList new_list;
    foreach(QString item, _list) {
        if (item == "") {
            continue;
        }
        new_list.append(item);
    }
    return new_list;
}

Emmet::Element* Emmet::copyElement(Element* e, short dollar_num) {
    Element* ele = new Element();
    ele->tagName = convertDollarSymbols(e->tagName, dollar_num);
    ele->id = convertDollarSymbols(e->id, dollar_num);
    foreach(QString cls, e->classlist) {
        ele->classlist << convertDollarSymbols(cls, dollar_num);
    }
    QHash<QString, QString>::iterator i;
    for (i = e->attrdict.begin(); i != e->attrdict.end(); ++i) {
        QString key = convertDollarSymbols(i.key(), dollar_num);
        QString val = convertDollarSymbols(i.value(), dollar_num);
        ele->attrdict[key] = val;
    }
    ele->text = convertDollarSymbols(e->text, dollar_num);
    foreach(Element * child, e->children) {
        ele->children << copyElement(child, dollar_num);
    }
    return  ele;
}
QList<Emmet::Element*> Emmet::multipleCopyElement(Element* e, short times) {
    if (times <= 0) {
        return { copyElement(e, 0) };
    }
    ushort i = 1;
    QList<Element*> elements;
    while (i <= times) {
        elements << copyElement(e, i);
        ++i;
    }
    return elements;
}
