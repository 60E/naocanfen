// Copyright (c) 2009-2010 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_AUXPOW_H
#define BITCOIN_AUXPOW_H

#include "main.h"

class CParentBlockHeader
{
public:
    // header
    static const int CURRENT_VERSION=2;
    int nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    unsigned int nTime;
    unsigned int nBits;
    unsigned int nNonce;

    CParentBlockHeader()
    {
        SetNull();
    }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(this->nVersion);
        nVersion = this->nVersion;
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
    )

    void SetNull()
    {
        nVersion = CParentBlockHeader::CURRENT_VERSION;
        hashPrevBlock = 0;
        hashMerkleRoot = 0;
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const
    {
        return Hash(BEGIN(nVersion), END(nNonce));
    }
};


class CAuxPow
{
public:
    CMerkleTx mMerkleTx;
    // Merkle branch with root vchAux
    // root must be present inside the coinbase
    std::vector<uint256> vChainMerkleBranch;
    // Index of chain in chains merkle tree
    int nChainIndex;
    CParentBlockHeader vParentBlockHeader;

    CAuxPow();
    CAuxPow(const CTransaction& txIn);
    
    bool Check(uint256 hashAuxBlock, int nChainID) const;

    uint256 GetParentBlockHash()
    {
        return vParentBlockHeader.GetHash();
    }

    uint256 GetPoWHash(int nBlockType) const;

    IMPLEMENT_SERIALIZE
    (
        READWRITE(mMerkleTx);
        READWRITE(vChainMerkleBranch);
        READWRITE(nChainIndex);
        READWRITE(vParentBlockHeader);
    )

};

extern void RemoveMergedMiningHeader(std::vector<unsigned char>& vchAux);
extern CScript MakeCoinbaseWithAux(unsigned int nBits, unsigned int nExtraNonce, std::vector<unsigned char>& vchAux);
extern void IncrementExtraNonceWithAux(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce, int64& nPrevTime, std::vector<unsigned char>& vchAux);
#endif

