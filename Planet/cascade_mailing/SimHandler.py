from __future__ import annotations
from .Settings import *
from .Storage import *
from .ConcurentHandler import *
from .SheduleHandler import *
from .ConflictHandler import *
from .TimeModel import *
from .Logger import *

class SimHandler:
    def __init__(self, settings: Settings, storage: Storage, protocolHandler, conflictHandler: ConflictHandler, 
                 timeModel: TimeModel, logger: Logger, maxTime):
        self.settings = settings
        self.storage = storage
        self.protocol = protocolHandler
        self.ch = conflictHandler
        self.tm = timeModel
        self.logger = logger
        
        self.max_time = maxTime
        
        self.storage.nodesWithMessage = []
        self.storage.targetNodes = []

        if not self.settings.loadTest:
            self.initNodesWithMessage(self.settings.margin, self.settings.margin)
        else:
            self.storage.nodesWithMessage = [i for i in range(len(self.storage.nodeCoords))]
        
        

        #for logging
        self.time_step = 1
        self.temp_time = 1
        
        #if type(protocolHandler) == SheduleHandler:
        #    ...
        #if type(protocolHandler) == ConcurentHandler:
        #    ...
        
    def initNodesWithMessage(self, fractionLeft: float, fractionRight: float):
        xRightCoord = self.settings.center[0] - self.settings.size[0]/2 + self.settings.size[0]*fractionLeft
        xLeftCoord = self.settings.center[0] + self.settings.size[0]/2 - self.settings.size[0]*fractionRight
        nodesWithMessage = []
        targetNodes = []
        for index in range(len(self.storage.nodeCoords)):
            if self.storage.nodeCoords[index][0] < xRightCoord:
                nodesWithMessage.append(index)
            if self.storage.nodeCoords[index][0] > xLeftCoord:
                targetNodes.append(index)
                
        self.storage.nodesWithMessage = nodesWithMessage
        self.storage.targetNodes = targetNodes
            
    def algStep(self, max_time):
        currNodesToProcess = self.tm.getNodesToProcess()
        #nextProcessList = self.processNodes(currNodesToProcess)
        self.__currentTime = currNodesToProcess[0]
        currProcessList = currNodesToProcess[1]
        self.updateNodesWithMessages(currProcessList)
        if self.settings.protocol == 1:
            interval, mesWantToHear = self.protocol.getNodesWhichWantToHear(currProcessList, self.storage.nodesWithMessage, self.__currentTime)
            nodesThatHear = self.didNodesHearSomething(mesWantToHear, interval[0], interval[1])
            self.protocol.updateStateList(mesWantToHear, nodesThatHear)
        if self.settings.protocol == 0:
            sendList, nextProcessList = self.protocol.processNodes(currProcessList, self.storage.nodesWithMessage, self.__currentTime)
        if self.settings.protocol == 1:
            sendList, nextProcessList = self.protocol.processNodes(currProcessList, self.storage.nodesWithMessage, self.__currentTime)
        self.tm.updateLastProcessTimes(currProcessList, self.__currentTime)
        if sendList != []:
            self.sendMessages(self.__currentTime, sendList)
        #print(self.__currentTime)
        if not self.settings.loadTest:    
            if set(self.storage.nodesWithMessage).intersection(self.storage.targetNodes) != set():
                print("point with message", set(self.storage.nodesWithMessage).intersection(self.storage.targetNodes))
                return False
        self.addNodesToProcess(nextProcessList)
        if self.tm.getCurrentTime() > max_time:
            return False
        return True
    
    def haveIncomingMessages(self, iNode: int):
        t_prev = self.tm.getNodePrevProcessTime(iNode)
        t_curr = self.tm.getCurrentTime()
        V_inc = self.storage.graph[iNode]

        #(16,48), (16,88)

        totalHasMessage = False
        for v_i in V_inc:
            hasMessage = False
            R = self.storage.interferenceRadius
            R_v_i_v = Calculations.dist(self.storage.nodeCoords[iNode], self.storage.nodeCoords[v_i])
            #without max
            t_v_i_v = R_v_i_v/R
            t_beg_i = t_prev - t_v_i_v
            t_end_i = t_curr - t_v_i_v
            hyst_i = self.tm.getTimesForNode(v_i, t_beg_i, t_end_i)
            edgeBeginEnd = (v_i, iNode)

            if hyst_i == []:
                continue
            #if hyst_i != []:
            #    print("here")
            edge = [v_i, iNode]
            edge.sort()
            V_i_per = self.ch.getConflictIndexes(v_i, iNode)
            V_i_per[0] = iNode
                
            t_beg_i_j = t_beg_i - 1
            t_end_i_j = t_end_i
            hyst_i_per = {}
            for v_i_per in V_i_per:
                hyst_v_i_per = self.tm.getTimesForNode(v_i_per, t_beg_i_j, t_end_i_j)
                hyst_i_per[v_i_per] = hyst_v_i_per
                
            times = self.ch.findSheduleConflict2(v_i, iNode, hyst_i, hyst_i_per)
            
            numberOfConflicts = len(hyst_i) - len(times)
            numberOfSendedTotal = len(hyst_i)
            self.logger.addCollisionsToNode([edgeBeginEnd], numberOfConflicts)
            self.logger.addTotalSendedToNode([edgeBeginEnd], numberOfSendedTotal)
            
            hasMessage = self.tryToSendMessage(v_i, iNode, times)
            totalHasMessage = totalHasMessage or hasMessage
        return totalHasMessage
            
    def tryToSendMessage(self, INode_S, INode_G, times):
        sended = False
        edge = [INode_S, INode_G]
        edge.sort()
        for t in times:
            probToSent = self.storage.edgesDict[tuple(edge)]
            p = self.rng.random()
            if p <= probToSent:
                sended = True
                break
        return sended
    
    def sendMessage(self, time, iNode):
        self.tm.addToSendedHistory((time, [iNode]))
        
    def sendMessages(self, time, INodeList):
        self.tm.addToSendedHistory((time, INodeList))
        
    def updateNodesWithMessages(self, InodeList):
        nodesWithoutMessage = list(set(InodeList) - set(self.storage.nodesWithMessage))
        newMes = []
        
        if self.settings.loadTest:
            for iNode in InodeList:
                self.haveIncomingMessages(iNode)
        
        for iNode in nodesWithoutMessage:
            haveNewMes = self.haveIncomingMessages(iNode)
            if haveNewMes:
                self.storage.nodesWithMessage.append(iNode)
                newMes.append(iNode)
             
        if newMes!=[]:
            print(newMes)
        return self.storage.nodesWithMessage
        
    def addNodesToProcess(self, nextProcessList):
        for processNodes in nextProcessList:
            self.tm.addNodesToProcess(processNodes)
            

    def didNodesHearSomething(self, iNodeList, timeBeg, timeEnd):
        nodesThatHeared = []
        for iNode in iNodeList:
            if self.didNodeHearSomething(iNode, timeBeg, timeEnd):
                nodesThatHeared.append(iNode)
                
        return nodesThatHeared

    def didNodeHearSomething(self, iNode, timeBeg, timeEnd):
        #hearSomething = False
        V_inc = self.storage.graph[iNode]
        for v_i in V_inc:
            R = self.storage.interferenceRadius
            R_v_i_v = Calculations.dist(self.storage.nodeCoords[iNode], self.storage.nodeCoords[v_i])
            t_v_i_v = R_v_i_v/R
            t_beg_i = timeBeg - t_v_i_v
            t_end_i = timeEnd - t_v_i_v
            hyst_i = self.tm.getTimesForNode(v_i, t_beg_i, t_end_i)
            if hyst_i != []:
                return True
            
        return False
    
    def resetSim(self):
        self.__currentTime = 0
        
        #for logging
        self.time_step = 10
        self.temp_time = 10
        
        self.storage.nodesWithMessage = []
        self.storage.targetNodes = []

        if not self.settings.loadTest:
            self.initNodesWithMessage(self.settings.margin, self.settings.margin)
        else:
            self.storage.nodesWithMessage = [i for i in range(len(self.storage.nodeCoords))]
        
    def doSim(self):
        self.__currentTime = self.tm.getCurrentTime()
        if self.settings.protocol == 0:
            currNodesToProcessLater = self.protocol.allNodesFirstProcessLists(self.__currentTime)
        if self.settings.protocol == 1:
            currNodesToProcessLater = self.protocol.allNodesFirstProcessLists(self.__currentTime)
        
        for listToProcess in currNodesToProcessLater:
            self.tm.addNodesToProcess(listToProcess)
            
        self.rng = np.random.default_rng(self.settings.seed)
        
        while self.algStep(self.max_time):
            if self.__currentTime > self.temp_time:
                print(self.__currentTime)
                self.temp_time = self.temp_time + self.time_step
                
        print("time", self.tm.getCurrentTimeInSec())