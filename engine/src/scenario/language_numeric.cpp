#include "language_internal.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <numeric>

namespace simforge::scenario::language
{
namespace detail
{
std::uint64_t product(std::uint64_t a,std::uint64_t b)
{
    if(b && a>std::numeric_limits<std::uint64_t>::max()/b) fail(Code::Overflow,"Numeric multiplication exceeds the exact integer range");
    return a*b;
}
Rational add(Rational a,Rational b)
{
    a=rational(a.numerator,a.denominator);b=rational(b.numerator,b.denominator);
    const auto gcd=std::gcd(a.denominator,b.denominator);
    const auto x=product(a.numerator,b.denominator/gcd),y=product(b.numerator,a.denominator/gcd);
    if(y>std::numeric_limits<std::uint64_t>::max()-x) fail(Code::Overflow,"Numeric addition exceeds the exact integer range");
    return rational(x+y,product(a.denominator,b.denominator/gcd));
}
Rational subtract(Rational a,Rational b)
{
    a=rational(a.numerator,a.denominator);b=rational(b.numerator,b.denominator);
    const auto gcd=std::gcd(a.denominator,b.denominator);
    const auto x=product(a.numerator,b.denominator/gcd),y=product(b.numerator,a.denominator/gcd);
    if(y>x) fail(Code::Range,"This change would produce a negative value");
    return rational(x-y,product(a.denominator,b.denominator/gcd));
}
Rational multiply(Rational a,Rational b)
{
    a=rational(a.numerator,a.denominator);b=rational(b.numerator,b.denominator);
    const auto x=std::gcd(a.numerator,b.denominator),y=std::gcd(b.numerator,a.denominator);
    return rational(product(a.numerator/x,b.numerator/y),product(a.denominator/y,b.denominator/x));
}
Rational divide(Rational a,Rational b)
{
    b=rational(b.numerator,b.denominator);if(!b.numerator) fail(Code::Range,"Division by zero or a zero arrival rate is unsupported");
    return multiply(a,{b.denominator,b.numerator});
}
void diagnose(std::vector<Diagnostic>& out,Code code,Stage stage,Span span,std::string message)
{
    Diagnostic diagnostic{Severity::Error,code,stage,span,std::move(message)};
    if(out.size()<max_diagnostics) out.push_back(std::move(diagnostic));
    else out.back()=std::move(diagnostic); // A full informational list must never hide a blocking error.
}
bool errors(std::span<const Diagnostic> diagnostics)
{return std::any_of(diagnostics.begin(),diagnostics.end(),[](const auto& d){return d.severity==Severity::Error;});}
std::optional<Unit> duration_unit(std::string_view s)
{
    if(s=="second"||s=="seconds"||s=="sec"||s=="secs"||s=="s") return Unit::Seconds;
    if(s=="minute"||s=="minutes"||s=="min"||s=="mins") return Unit::Minutes;
    if(s=="hour"||s=="hours"||s=="hr"||s=="hrs"||s=="h") return Unit::Hours;
    return {};
}
std::optional<unsigned> word_number(std::string_view text)
{
    constexpr std::array<std::string_view,20> words{"zero","one","two","three","four","five","six","seven","eight","nine",
        "ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen"};
    for(unsigned i=0;i<words.size();++i) if(text==words[i]) return i;
    constexpr std::array<std::string_view,8> tens{"twenty","thirty","forty","fifty","sixty","seventy","eighty","ninety"};
    for(unsigned i=0;i<tens.size();++i) if(text==tens[i]) return (i+2)*10;
    return {};
}
std::optional<Quantity> quantity(std::span<const Token> t,std::size_t begin)
{
    if(begin>=t.size()) return {};
    if(t[begin].kind==TokenKind::Hyphen) fail(Code::InvalidNumber,"Use a nonnegative quantity; express removal with subtract/remove");
    if(t[begin].kind==TokenKind::Number) return Quantity{parse_number(t[begin].text),begin+1};
    if(t[begin].text=="nan"||t[begin].text=="inf"||t[begin].text=="infinity") fail(Code::InvalidNumber,"NaN and infinity are unsupported");
    const auto n=word_number(t[begin].text);if(!n) return {};
    auto end=begin+1;unsigned result=*n;
    if(end<t.size() && t[end].text=="hundred" && *n>=1 && *n<=9) {
        result*=100;++end;
        if(end<t.size()) if(const auto tail=word_number(t[end].text);tail && *tail>0 && *tail<100) {result+=*tail;++end;}
    }
    if(result%10==0 && result%100>=20 && result%100<=90) {
        const auto next=end<t.size()&&t[end].kind==TokenKind::Hyphen?end+1:end;
        if(next<t.size()) if(const auto tail=word_number(t[next].text);tail && *tail>0 && *tail<10) {result+=*tail;end=next+1;}
    }
    return Quantity{{result,1},end};
}
}
Rational rational(std::uint64_t numerator,std::uint64_t denominator)
{
    if(!denominator) detail::fail(Code::InvalidNumber,"A rational denominator cannot be zero");
    const auto gcd=std::gcd(numerator,denominator);return {numerator/gcd,denominator/gcd};
}
Rational parse_number(std::string_view text)
{
    if(text.empty()||text.size()>32) detail::fail(Code::InvalidNumber,"Numeric token is empty or too long");
    const auto dot=text.find('.'); const auto decimals=dot==std::string_view::npos?0:text.size()-dot-1;
    if(decimals>9 || (dot!=std::string_view::npos && (!dot || !decimals))) detail::fail(Code::InvalidNumber,"Use an integer or decimal with at most nine fractional digits");
    std::uint64_t numerator=0,denominator=1;
    for(std::size_t i=0;i<text.size();++i) {
        const auto c=text[i];if(c=='.' && i==dot) continue;
        if(c<'0'||c>'9') detail::fail(Code::InvalidNumber,"Unsupported numeric notation or fraction");
        numerator=detail::product(numerator,10);
        if(static_cast<unsigned>(c-'0')>std::numeric_limits<std::uint64_t>::max()-numerator) detail::fail(Code::Overflow,"Numeric value exceeds uint64");
        numerator+=c-'0';if(dot!=std::string_view::npos && i>dot) denominator*=10;
    }
    return rational(numerator,denominator);
}
std::string format(Rational v)
{v=rational(v.numerator,v.denominator);return std::to_string(v.numerator)+(v.denominator==1?"":"/"+std::to_string(v.denominator));}
Tokenization tokenize(std::string_view text)
{
    Tokenization result;
    if(text.size()>max_input_bytes) {detail::diagnose(result.diagnostics,Code::InputLimit,Stage::Tokenization,{0,max_input_bytes},"Description exceeds 8192 bytes");return result;}
    const auto letter=[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');};
    const auto digit=[](unsigned char c){return c>='0'&&c<='9';};
    std::size_t at=0;
    while(at<text.size()) {
        const auto start=at;const auto c=static_cast<unsigned char>(text[at]);TokenKind kind;
        if(c==' '||c=='\t'||c=='\r') {++at;continue;}
        if(letter(c)) {kind=TokenKind::Word;while(at<text.size()&&letter(static_cast<unsigned char>(text[at]))) ++at;}
        else if(digit(c)) {
            kind=TokenKind::Number;while(at<text.size()&&digit(static_cast<unsigned char>(text[at]))) ++at;
            if(at+1<text.size()&&text[at]=='.'&&digit(static_cast<unsigned char>(text[at+1]))) {++at;while(at<text.size()&&digit(static_cast<unsigned char>(text[at]))) ++at;}
        } else {
            ++at;
            if(c=='%') kind=TokenKind::Percent;
            else if(c=='-') kind=TokenKind::Hyphen;
            else if(c==':') kind=TokenKind::Colon;
            else if(c==','||c=='.'||c==';'||c=='!'||c=='?'||c=='\n') kind=TokenKind::Boundary;
            else {
                while(at<text.size()&&static_cast<unsigned char>(text[at])>=128) ++at;
                detail::diagnose(result.diagnostics,Code::UnsupportedCharacter,Stage::Tokenization,{start,at-start},
                    "Supported descriptions use English letters, decimal digits, whitespace, punctuation, hyphens and percent symbols");
                continue;
            }
        }
        if(result.tokens.size()==max_tokens) {detail::diagnose(result.diagnostics,Code::InputLimit,Stage::Tokenization,{start,text.size()-start},"Description exceeds 1024 tokens");break;}
        std::string token(text.substr(start,at-start));for(auto& ch:token) if(ch>='A'&&ch<='Z') ch=static_cast<char>(ch-'A'+'a');
        result.tokens.push_back({kind,std::move(token),{start,at-start}});
    }
    return result;
}
}
