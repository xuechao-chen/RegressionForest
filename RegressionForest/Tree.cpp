#include "Tree.h"
#include <numeric>
#include <stack>
#include <queue>
#include <boost/format.hpp>
#include "common/HiveCommonMicro.h"
#include "common/ProductFactoryData.h"
#include "math/RandomInterface.h"
#include "TrainingSet.h"
#include "RegressionForestCommon.h"
#include "RegressionForestConfig.h"
#include "BaseInstanceWeightMethod.h"
#include "BaseFeatureWeightMethod.h"
#include "ObjectPool.h"
#include "SingleResponseNode.h"
#include "MultiResponseNode.h"

using namespace hiveRegressionForest;

CTree::CTree()
{
}

CTree::~CTree()
{
	// NOTES : boost object_pool automatically released
}

//****************************************************************************************************
//FUNCTION:
void CTree::buildTree(IBootstrapSelector* vBootstrapSelector, IFeatureSelector* vFeatureSelector, INodeSpliter* vNodeSpliter, IBaseTerminateCondition* vTerminateCondition, IFeatureWeightGenerator* vFeatureWeightMethod)
{
	//NOTES : 为避免划分过程中存储每个节点的CurrBootstrap，节点划分过程中仅保留一份BootstrapIndex
	//划分后交换Index，将左儿子的都保留在vector左边，右儿子保留在右边，用Range记录左右儿子BootStrap范围
	//Range: first记录第一个元素，second记录最后一个元素的后一位

	std::vector<int> BootstrapIndex;
	__initTreeParameters(vBootstrapSelector, BootstrapIndex);
	_ASSERTE(!m_pRoot);

	m_pRoot = __createNode(1);

	std::stack<std::pair<CNode*, std::pair<int, int> >> NodeBootstrapRangeStack;
	NodeBootstrapRangeStack.push({ m_pRoot,{ 0, BootstrapIndex.size() } });
		
	int RangeSplitPos = 0;
	bool IsUpdatingFeaturesWeight = CRegressionForestConfig::getInstance()->getAttribute<bool>(KEY_WORDS::LIVE_UPDATE_FEATURES_WEIGHT);
	std::vector<int> CurrFeatureIndexSubSet;
	std::pair<std::vector<std::vector<float>>, std::vector<float>> BootstrapDataset;

	while (!NodeBootstrapRangeStack.empty())
	{
		CNode* pCurNode = NodeBootstrapRangeStack.top().first;
		_ASSERTE(pCurNode);

		std::pair<int, int> CurBootstrapRange = NodeBootstrapRangeStack.top().second;
		NodeBootstrapRangeStack.pop();

		CTrainingSet::getInstance()->recombineBootstrapDataset(BootstrapIndex, CurBootstrapRange, BootstrapDataset);//TODO: optimize!!!

		_ASSERTE(vTerminateCondition);
		if (vTerminateCondition->isMeetTerminateConditionV(BootstrapDataset.first, BootstrapDataset.second, __getTerminateConditionExtraParameter(pCurNode)))
		{
			__createLeafNode(pCurNode, CurBootstrapRange, BootstrapDataset);
		}
		else
		{
			CurrFeatureIndexSubSet.clear();

			_selectCandidateFeaturesV(vFeatureSelector, vFeatureWeightMethod, IsUpdatingFeaturesWeight, BootstrapDataset, CurrFeatureIndexSubSet);

			// NOTES : 由于 splitNode 过程如果发现某一边为空，则该节点不再进行划分，即又一种节点划分终止条件, 以
			//         splitNode 函数返回值表示，如false，则不再划分
			if ((vNodeSpliter)->splitNode(pCurNode, CurBootstrapRange, CurrFeatureIndexSubSet, BootstrapIndex, RangeSplitPos))
			{
				pCurNode->setLeftChild(__createNode(pCurNode->getLevel() + 1));
				pCurNode->setRightChild(__createNode(pCurNode->getLevel() + 1));
				NodeBootstrapRangeStack.push(std::make_pair(const_cast<CNode*>(&pCurNode->getLeftChild()), std::pair<int, int>(CurBootstrapRange.first, RangeSplitPos)));
				NodeBootstrapRangeStack.push(std::make_pair(const_cast<CNode*>(&pCurNode->getRightChild()), std::pair<int, int>(RangeSplitPos, CurBootstrapRange.second)));
			}
			else
			{
				__createLeafNode(pCurNode, CurBootstrapRange, BootstrapDataset);
			}
		}
	}

	__obtainOOBIndex(BootstrapIndex);
}

//****************************************************************************************************
//FUNCTION:
float CTree::predict(const CNode& vCurLeafNode, const std::vector<float>& vFeatures, float& voWeight, unsigned int vResponseIndex /*=0*/) const
{
	_ASSERTE(vCurLeafNode.isLeafNode() && !vFeatures.empty());

	voWeight = vCurLeafNode.calculateNodeWeight(vResponseIndex);

	return vCurLeafNode.predictV(vFeatures, vResponseIndex);
}

//****************************************************************************************************
//FUNCTION:
void CTree::fetchTreeInfo(STreeInfo& voTreeInfo) const
{
	std::vector<const CNode*> AllTreeNodes;
	__dumpAllTreeNodes(AllTreeNodes);

	voTreeInfo.m_FeatureSplitTimes.resize(CTrainingSet::getInstance()->getNumOfFeatures(), 0);
	for (auto TreeNode : AllTreeNodes)
	{
		++voTreeInfo.m_NumOfNodes;
		if (TreeNode->isUnfitted()) ++voTreeInfo.m_NumOfUnfittedLeafNodes;
		
		if (TreeNode->isLeafNode()) ++voTreeInfo.m_NumOfLeafNodes;
		else ++voTreeInfo.m_FeatureSplitTimes[TreeNode->getBestSplitFeatureIndex()];
	}

	const std::vector<int>& OOBIndexSet = this->getOOBIndexSet();
	voTreeInfo.m_InstanceOOBTimes.resize(CTrainingSet::getInstance()->getNumOfInstances(), 0);
	for (auto Itr : OOBIndexSet) voTreeInfo.m_InstanceOOBTimes[Itr]++;
}

//********************************************************************************************************
//FUNCTION:
bool CTree::operator==(const CTree& vTree) const
{
	return m_pRoot->operator==(vTree.getRoot());
}

//****************************************************************************************************
//FUNCTION:
CNode* CTree::__createNode(unsigned int vLevel)
{
	std::string NodeTypeSig;
	if(CRegressionForestConfig::getInstance()->isAttributeExisted(KEY_WORDS::CREATE_NODE_TYPE)) NodeTypeSig = CRegressionForestConfig::getInstance()->getAttribute<std::string>(KEY_WORDS::CREATE_NODE_TYPE);
	else NodeTypeSig = KEY_WORDS::SINGLE_RESPONSE_NODE;
	
	if (NodeTypeSig.empty() || NodeTypeSig == KEY_WORDS::SINGLE_RESPONSE_NODE)
	{
		return CRegressionForestObjectPool<CSingleResponseNode>::getInstance()->allocateNode(vLevel);
	}
	else if (NodeTypeSig == KEY_WORDS::MULTI_RESPONSES_NODE)
	{
		return CRegressionForestObjectPool<CMultiResponseNode>::getInstance()->allocateNode(vLevel);
	}
	else
	{
		_ASSERTE(false);
	}
	
}

//****************************************************************************************************
//FUNCTION:
void CTree::__createLeafNode(CNode* vCurNode, const std::pair<int, int>& vRange, const std::pair<std::vector<std::vector<float>>, std::vector<float>>& vBootstrapDataset)
{
	vCurNode->createAsLeafNodeV(vBootstrapDataset);
	vCurNode->setNodeSize(vRange.second - vRange.first);

	vCurNode->setLeafNodeId(m_LeafNodeId++);
}

//****************************************************************************************************
//FUNCTION:
boost::any CTree::__getTerminateConditionExtraParameter(const CNode* vNode)
{
	return boost::any(vNode->getLevel());
}

//****************************************************************************************************
//FUNCTION:
void CTree::_selectCandidateFeaturesV(IFeatureSelector* vFeatureSelector, IFeatureWeightGenerator* vFeatureWeightMethod, bool vIsUpdatingFeaturesWeight, const std::pair<std::vector<std::vector<float>>, std::vector<float>>& vBootstrapDataset, std::vector<int>& voCandidateFeaturesIndex)
{
	_ASSERTE(!vBootstrapDataset.first.empty());

	static bool IsFeatureWeighted = (CRegressionForestConfig::getInstance()->getAttribute<std::string>(KEY_WORDS::FEATURE_SELECTOR) == (KEY_WORDS::WEIGHTED_FEATURE_SELECTOR));

	static std::vector<float> FeaturesWeightSet;
	if (IsFeatureWeighted)
	{
		__updateFeaturesWeight(vFeatureWeightMethod, vIsUpdatingFeaturesWeight, vBootstrapDataset, FeaturesWeightSet);
	}

	vFeatureSelector->generateFeatureIndexSetV(CTrainingSet::getInstance()->getNumOfFeatures(), voCandidateFeaturesIndex, FeaturesWeightSet);
}

//****************************************************************************************************
//FUNCTION:
void CTree::__initTreeParameters(IBootstrapSelector* vBootstrapSelector, std::vector<int>& voBootstrapIndex)
{
	_ASSERTE(vBootstrapSelector);

	std::string BootstrapSelectorSig = CRegressionForestConfig::getInstance()->getAttribute<std::string>(KEY_WORDS::BOOTSTRAP_SELECTOR);
	std::vector<float> InstanceWeightSet;
	
#pragma omp critical
	{
		if (BootstrapSelectorSig == KEY_WORDS::WEIGHTED_BOOTSTRAP_SELECTOR)
		{
			IBaseInstanceWeight* pBaseInstanceWeight = dynamic_cast<IBaseInstanceWeight*>(hiveOO::CProductFactoryData::getInstance()->createProduct(CRegressionForestConfig::getInstance()->getAttribute<std::string>(KEY_WORDS::INSTANCE_WEIGHT_CALCULATE_METHOD)));
			pBaseInstanceWeight->generateInstancesWeightV(CTrainingSet::getInstance()->getNumOfInstances(), InstanceWeightSet);

			_SAFE_DELETE(pBaseInstanceWeight);
		}
	}
	vBootstrapSelector->generateBootstrapIndexSetV(CTrainingSet::getInstance()->getNumOfInstances(), voBootstrapIndex, InstanceWeightSet);
}

//********************************************************************************************************
//FUNCTION:
void CTree::__obtainOOBIndex(std::vector<int>& vBootStrapIndexSet)
{
	_ASSERTE(!vBootStrapIndexSet.empty() && vBootStrapIndexSet.size() == CTrainingSet::getInstance()->getNumOfInstances());

	std::sort(vBootStrapIndexSet.begin(), vBootStrapIndexSet.end());

	int BootstrapIndex = 0, TrainingSetIndex = 0;
	int TrainingSetSize = CTrainingSet::getInstance()->getNumOfInstances();
	while (TrainingSetIndex < TrainingSetSize && BootstrapIndex < vBootStrapIndexSet.size())
	{
		if (TrainingSetIndex < vBootStrapIndexSet[BootstrapIndex]) m_OOBIndexSet.push_back(TrainingSetIndex++);
		else if (TrainingSetIndex == vBootStrapIndexSet[BootstrapIndex]) ++TrainingSetIndex, ++BootstrapIndex;
		else ++BootstrapIndex;
	}
	for (; TrainingSetIndex < TrainingSetSize; ++TrainingSetIndex) m_OOBIndexSet.push_back(TrainingSetIndex);
}

//****************************************************************************************************
//FUNCTION:
void CTree::__dumpAllTreeNodes(std::vector<const CNode*>& voAllTreeNodes) const
{
	std::queue<const CNode*> q;
	q.push(m_pRoot);

	while (!q.empty())
	{
		voAllTreeNodes.push_back(q.front());
		const CNode* pTempNode = q.front();
		q.pop();
		if (&pTempNode->getLeftChild()) q.push(&pTempNode->getLeftChild());
		if (&pTempNode->getRightChild()) q.push(&pTempNode->getRightChild());
	}
}

//****************************************************************************************************
//FUNCTION:
void CTree::__updateFeaturesWeight(IFeatureWeightGenerator* vFeatureWeightMethod, bool vIsLiveUpdating, const std::pair<std::vector<std::vector<float>>, std::vector<float>>& vBootstrapDataset, std::vector<float>& voFeaturesWeight)
{
	if (!vIsLiveUpdating && vBootstrapDataset.first.size() != CTrainingSet::getInstance()->getNumOfInstances()) return;

	voFeaturesWeight.clear();
	std::vector<std::pair<unsigned int, float>> VariableImportanceSortedList;
	vFeatureWeightMethod->generateFeatureWeight(vBootstrapDataset.first, vBootstrapDataset.second, VariableImportanceSortedList);

	voFeaturesWeight.resize(VariableImportanceSortedList.size());
	std::for_each(VariableImportanceSortedList.begin(), VariableImportanceSortedList.end(), [&](const std::pair<unsigned int, float> &vVariableImportance) { voFeaturesWeight[vVariableImportance.first] = vVariableImportance.second; });
}

//****************************************************************************************************
//FUNCTION:
const CNode* CTree::locateLeafNode(const std::vector<float>& vFeatures) const
{
	_ASSERTE(m_pRoot);

	std::stack<const CNode*> NodeStack; //NOTES : 指针占用内存小
	NodeStack.push(m_pRoot);

	while (!NodeStack.empty())
	{
		const CNode* pCurrNode = NodeStack.top();
		if (pCurrNode->isLeafNode())
		{			
			return pCurrNode;
		}

		NodeStack.pop();
		if (vFeatures[pCurrNode->getBestSplitFeatureIndex()] < pCurrNode->getBestGap())
			NodeStack.push(&pCurrNode->getLeftChild());
		else
			NodeStack.push(&pCurrNode->getRightChild());
	}
}

//****************************************************************************************************
//FUNCTION:
void CTree::fetchOOBInLeafNode(std::vector<int>& voOOBInLeafNode) const
{
	_ASSERTE(!m_OOBIndexSet.empty());

	for (auto Index : m_OOBIndexSet)
	{
		const CNode* CurLeafNode = locateLeafNode(CTrainingSet::getInstance()->getFeatureInstanceAt(Index));
		voOOBInLeafNode.push_back(CurLeafNode->getLeafNodeId());
	}
}