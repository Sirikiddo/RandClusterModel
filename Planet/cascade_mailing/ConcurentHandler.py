import random
import bisect
from itertools import groupby
from .Deployment import Calculations
import numpy as np

class ConcurentHandler:
    def __init__(self, nodeCoords):
        
        #secs
        self.__slotLength = 0.0125
        self.__nSlotLength = Calculations.fromSecToUnit(0.0125)
        self.__powLength = 3
        self.__superFrameLength = 2**self.__powLength
        self.__lengthWindowForProtocol = 12
        self.__durationOfHearing = 2
        
        #значение - число удачных прослушиваний канала.
        self.__stateList = [self.__durationOfHearing for _ in range(len(nodeCoords))]
        
    def updateState(self, iNode, hearMessage):
        currState = self.__stateList[iNode]
        if not hearMessage:
            currState = currState - 1
        else:
            currState = self.__durationOfHearing
            
        self.__stateList[iNode] = currState
        
    def updateStateList(self, furhterProcessNodes, nodesThatHear):
        for iNode in nodesThatHear:
            self.updateState(iNode, True)
        nodesThatNotHear = list(set(furhterProcessNodes) - set(nodesThatHear))
        for iNode in nodesThatNotHear:
            self.updateState(iNode, False)
        
    def nextProcessTime(self, iNode, currTime, hearMessage):
        currState = self.__stateList[iNode]
        next_time = currTime
        
        currState = self.__stateList[iNode]
        if not hearMessage:
            currState = currState - 1
        else:
            currState = self.__durationOfHearing

        #отправить сообщение в этот слот
        if currState == 0:
            #currState = self.__durationOfHearing
            next_time = next_time
            
        if currState == self.__durationOfHearing:
            w = 2**self.__lengthWindowForProtocol
            next_num = random.randint(0, w - 1)
            currState = self.__durationOfHearing
            next_time = next_time + next_num * self.__nSlotLength
           
        return next_time
            
    def allNodesFirstProcessLists(self, currentTime: float):
        listTimeListPairs = []
        lookup = {}
        for ind in range(len(self.__stateList)):
            offset = self.randOffset()
            time = currentTime + offset
            if time not in lookup:
                target = lookup[time] = (time, [ind])
                listTimeListPairs.append(target)
            else:
                lookup[time][1].append(ind)
            
            
        return listTimeListPairs
    
    def randOffset(self):
        w = 2**self.__lengthWindowForProtocol
        next_num = random.randint(0, w - 1)
        res = (next_num + 1) * self.__nSlotLength
        return res
    
    def sendMessage(self, iNode):
        canSend = False
        if self.__stateList[iNode] == 0:
            canSend = True
        return canSend
    
    def processNode(self, iNode, hearMes):
        self.updateState(iNode, hearMes)
        nodesReadyToSend = self.getNodesReadyToSend()

    def getNodesReadyToSend(self):
        nodesReadyToSend = [i for i, x in enumerate(self.__stateList) if x == 0]
        return nodesReadyToSend
        
    def getNodesWhichWantToHear(self, INodeList: list[int], nodesWithMessage: list[int], currentTime: float):
        interval = (currentTime - self.__nSlotLength, currentTime)
        furtherProcessNodes = list(set(INodeList).intersection(nodesWithMessage))
        return interval, furtherProcessNodes
    
    def processNodes(self, INodeList: list[int], nodesWithMessage: list[int], currentTime: float):
        furtherProcessNodes = list(set(INodeList).intersection(nodesWithMessage))
        anotherNodes = list(set(INodeList) - set(nodesWithMessage))
        atuallySendNodes = []
        listTimeListPairs = []
        lookup = {}
        for iNode in furtherProcessNodes:
            if self.sendMessage(iNode):
                atuallySendNodes.append(iNode)
                #change status
                self.__stateList[iNode] = self.__durationOfHearing
                #calculate next process times
                offset = self.randOffset()
                time = currentTime + offset
            else:
                #calculate next process times
                time = currentTime + self.__nSlotLength
            if time not in lookup:
                target = lookup[time] = (time, [iNode])
                listTimeListPairs.append(target)
            else:
                lookup[time][1].append(iNode)
                
        for iNode in anotherNodes:
            #calculate next process times
            offset = self.randOffset()
            time = currentTime + offset
            if time not in lookup:
                target = lookup[time] = (time, [iNode])
                listTimeListPairs.append(target)
            else:
                lookup[time][1].append(iNode)
                
        return atuallySendNodes, listTimeListPairs
    
    def getTimesForNode(self, histList, time_beg: float, time_end: float):
        currNodeHystory = histList
        
        i = bisect.bisect_left(currNodeHystory, time_beg)
        j = bisect.bisect_right(currNodeHystory, time_end)
        res = currNodeHystory[i:j]
        return res

    def getFreeTicksList(self, hearList, lastTime):
        ticksList = [[] for _ in range(len(self.__stateList))]
        for iNode in range(len(self.__stateList)):
            begin_time = 0
            end_time = begin_time + self.__nSlotLength
            while begin_time < lastTime:
                res = self.getTimesForNode(hearList[iNode], begin_time, end_time)
                ticksList[iNode].append(len(res))
                begin_time = end_time
                end_time = end_time + self.__nSlotLength
                
        return ticksList
    
    def func_cnt(self, ticksList, zeroSize):
        res = [0 for _ in range(len(ticksList))]
        p = [0 for _ in range(len(ticksList))]
        for ind, arr in enumerate(ticksList):
            inZero = False
            beg_ind = 0
            end_ind = 0
            for i, el in enumerate(arr):
                if el == 0 and not inZero:
                    beg_ind = i
                    inZero = True
                if el != 0 and inZero:
                    end_ind = i
                    inZero = False
                    res[ind] = res[ind] + end_ind - beg_ind - zeroSize + 1
            p[ind] = res[ind] / len(arr)
        return p

    def reset(self):
        self.__stateList = [self.__durationOfHearing for _ in range(len(self.__stateList))]