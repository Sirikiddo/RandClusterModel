from __future__ import annotations
import queue
import bisect

from cascade_mailing.Deployment import Calculations

class TimeModel:
    def __init__(self, nodeCoords):
        self.__sendedHistory = [[] for _ in range(len(nodeCoords))]
        self.__lastProcessTime = [0 for _ in range(len(nodeCoords))]
        self.__nodesToProcess = queue.PriorityQueue(maxsize=0)
        self.__currentTime = 0
        
    def addNodesToProcess(self, arg: tuple[float, list[int]]):
        #arg = (time, iNodeList)
        self.__nodesToProcess.put(arg)
        
    def getNodesToProcess(self):
        if not self.__nodesToProcess.empty():
            elem = self.__nodesToProcess.get()
            self.__currentTime = elem[0]
            processList = elem[1]
            while not self.__nodesToProcess.empty() and self.__nodesToProcess.queue[0][0] == self.__currentTime:
                processList = processList + self.__nodesToProcess.get()[1]
            return (self.__currentTime, processList)
        else:
            print("no elems in queue")
        
    def getCurrentTime(self):
        return self.__currentTime
    
    def getCurrentTimeInSec(self):
        return Calculations.fromUnitToSec(self.__currentTime)
        
    def addToSendedHistory(self: TimeModel, data_el: tuple[float, list[int]]):
        for currNode in data_el[1]:
            self.__sendedHistory[currNode].append(data_el[0])
        
    #def getHistory(self, time_beg, time_end):
    #    i = bisect.bisect_left(self.__sendedHistory, (time_beg,))
    #    j = bisect.bisect_left(self.__sendedHistory, (time_end,))
    #    return self.__sendedHistory[i:j]
    
    def getTimesForNode(self, iNode: int, time_beg: float, time_end: float):
        currNodeHystory = self.__sendedHistory[iNode]
        
        i = bisect.bisect_left(currNodeHystory, time_beg)
        j = bisect.bisect_right(currNodeHystory, time_end)
        res = currNodeHystory[i:j]
        return res
    
    def getLastProcessTime(self: TimeModel, iNode: int):
        return self.__lastProcessTime[iNode]
    
    def updateLastProcessTimes(self: TimeModel, iNodeList, lastProcessTime: float):
        for iNode in iNodeList:
            self.__lastProcessTime[iNode] = lastProcessTime

    def updateLastProcessTime(self: TimeModel, iNode: int, lastProcessTime: float):
        self.__lastProcessTime[iNode] = lastProcessTime

    def getNodePrevProcessTime(self: TimeModel, iNode: int):
        return self.__lastProcessTime[iNode]
    
    def getSendedHistory(self: TimeModel):
        return self.__sendedHistory
    
    def reset(self: TimeModel):
        self.__sendedHistory = [[] for _ in range(len(self.__sendedHistory))]
        self.__lastProcessTime = [0 for _ in range(len(self.__lastProcessTime))]
        self.__nodesToProcess = queue.PriorityQueue(maxsize=0)
        self.__currentTime = 0