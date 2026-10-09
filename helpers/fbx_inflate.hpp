#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace FbxInflate {
namespace detail {
struct Bits {
    const uint8_t* data;
    size_t size;
    size_t byte=0;
    unsigned bit=0;

    bool read(unsigned count,uint32_t& value) {
        value=0;
        for(unsigned i=0;i<count;i++) {
            if(byte>=size)return false;
            value|=(uint32_t)((data[byte]>>bit)&1u)<<i;
            if(++bit==8){bit=0;++byte;}
        }
        return true;
    }
    void align(){if(bit){bit=0;++byte;}}
};

struct Huffman {
    std::array<uint16_t,16> count{};
    std::vector<uint16_t> symbols;
    unsigned maxBits=0;

    bool build(const std::vector<uint8_t>& lengths) {
        count.fill(0);maxBits=0;
        for(uint8_t n:lengths){if(n>15)return false;if(n){++count[n];maxBits=std::max(maxBits,(unsigned)n);}}
        int left=1;
        for(unsigned n=1;n<=15;n++){left=(left<<1)-count[n];if(left<0)return false;}
        if(!maxBits)return false;
        std::array<uint16_t,16> offsets{};
        for(unsigned n=1;n<15;n++)offsets[n+1]=offsets[n]+count[n];
        symbols.resize(lengths.size());
        for(size_t symbol=0;symbol<lengths.size();symbol++)if(lengths[symbol])symbols[offsets[lengths[symbol]]++]=(uint16_t)symbol;
        return true;
    }
    bool decode(Bits& bits,uint16_t& symbol) const {
        unsigned code=0,first=0,index=0;
        for(unsigned length=1;length<=maxBits;length++){
            uint32_t next;if(!bits.read(1,next))return false;code|=next;
            unsigned n=count[length];
            if(code>=first&&code-first<n){symbol=symbols[index+(code-first)];return true;}
            index+=n;first=(first+n)<<1;code<<=1;
        }
        return false;
    }
};

inline bool compressedBlock(Bits& bits,Huffman& litlen,Huffman& distance,
                            uint8_t* output,size_t capacity,size_t& out) {
    static constexpr uint16_t lengthBase[]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static constexpr uint8_t lengthExtra[]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static constexpr uint16_t distanceBase[]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static constexpr uint8_t distanceExtra[]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    while(true){
        uint16_t symbol;if(!litlen.decode(bits,symbol))return false;
        if(symbol<256){if(out>=capacity)return false;output[out++]=(uint8_t)symbol;continue;}
        if(symbol==256)return true;
        if(symbol<257||symbol>285)return false;
        size_t ix=symbol-257;uint32_t extra=0;
        if(!bits.read(lengthExtra[ix],extra))return false;
        size_t length=lengthBase[ix]+extra;
        uint16_t distSymbol;if(!distance.decode(bits,distSymbol)||distSymbol>=30)return false;
        extra=0;if(!bits.read(distanceExtra[distSymbol],extra))return false;
        size_t dist=distanceBase[distSymbol]+extra;
        if(dist>out||length>capacity-out)return false;
        for(size_t i=0;i<length;i++)output[out+i]=output[out+i-dist];
        out+=length;
    }
}

inline bool dynamicTrees(Bits& bits,Huffman& litlen,Huffman& distance) {
    uint32_t a,b,c;if(!bits.read(5,a)||!bits.read(5,b)||!bits.read(4,c))return false;
    size_t nl=a+257,nd=b+1,nc=c+4;
    static constexpr uint8_t order[]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    std::vector<uint8_t> codeLengths(19,0);
    for(size_t i=0;i<nc;i++){if(!bits.read(3,a))return false;codeLengths[order[i]]=(uint8_t)a;}
    Huffman codeTree;if(!codeTree.build(codeLengths))return false;
    std::vector<uint8_t> lengths;lengths.reserve(nl+nd);
    while(lengths.size()<nl+nd){
        uint16_t symbol;if(!codeTree.decode(bits,symbol))return false;
        if(symbol<16){lengths.push_back((uint8_t)symbol);continue;}
        uint32_t repeat=0;uint8_t value=0;
        if(symbol==16){if(lengths.empty()||!bits.read(2,repeat))return false;repeat+=3;value=lengths.back();}
        else if(symbol==17){if(!bits.read(3,repeat))return false;repeat+=3;}
        else if(symbol==18){if(!bits.read(7,repeat))return false;repeat+=11;}
        else return false;
        if(repeat>nl+nd-lengths.size())return false;
        lengths.insert(lengths.end(),repeat,value);
    }
    std::vector<uint8_t> literal(lengths.begin(),lengths.begin()+nl);
    std::vector<uint8_t> distances(lengths.begin()+nl,lengths.end());
    return literal.size()>256&&literal[256]!=0&&litlen.build(literal)&&distance.build(distances);
}
}

// Decode an FBX zlib-wrapped array into its known, validated raw size.
inline bool decode(const uint8_t* input,size_t inputSize,uint8_t* output,size_t outputSize) {
    if(!input||!output||inputSize<6||input[0]%16!=8||(((unsigned)input[0]<<8)|input[1])%31!=0||(input[1]&0x20))return false;
    detail::Bits bits{input+2,inputSize-6};size_t out=0;bool final=false;
    while(!final){
        uint32_t last,type;if(!bits.read(1,last)||!bits.read(2,type))return false;final=last!=0;
        if(type==0){
            bits.align();uint32_t len,nlen;if(!bits.read(16,len)||!bits.read(16,nlen)||(len^(uint32_t)0xffff)!=nlen||len>outputSize-out)return false;
            for(uint32_t i=0;i<len;i++){uint32_t v;if(!bits.read(8,v))return false;output[out++]=(uint8_t)v;}
        } else if(type==1||type==2) {
            detail::Huffman litlen,distance;
            if(type==1){
                std::vector<uint8_t> l(288),d(32,5);
                std::fill(l.begin(),l.begin()+144,8);std::fill(l.begin()+144,l.begin()+256,9);std::fill(l.begin()+256,l.begin()+280,7);std::fill(l.begin()+280,l.end(),8);
                if(!litlen.build(l)||!distance.build(d))return false;
            }else if(!detail::dynamicTrees(bits,litlen,distance))return false;
            if(!detail::compressedBlock(bits,litlen,distance,output,outputSize,out))return false;
        } else return false;
    }
    if(out!=outputSize)return false;
    uint32_t s1=1,s2=0;
    for(size_t i=0;i<out;i++){s1=(s1+output[i])%65521;s2=(s2+s1)%65521;}
    uint32_t expected=(uint32_t(input[inputSize-4])<<24)|(uint32_t(input[inputSize-3])<<16)|(uint32_t(input[inputSize-2])<<8)|input[inputSize-1];
    return ((s2<<16)|s1)==expected;
}
}
