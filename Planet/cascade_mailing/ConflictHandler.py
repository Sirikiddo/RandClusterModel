from __future__ import annotations
from typing import Optional
from .Deployment import Calculations
import numpy as np

class ConflictHandler:
    def __init__(self, pointList: list[list[float]], fVal: float, funcType: int, graph: list[list[int]]):
        self.__fVal = fVal
        self.__pointList = pointList
        
        self.__messageLength = 0.02
        
        self.__t_d = Calculations.fromSecToUnit(self.__messageLength)
        
        if funcType == 1:
            self.__p = np.vectorize(Calculations.p1)
        elif funcType == 2:
            self.__p = np.vectorize(Calculations.p2)
        else:
            print("incorrect probability function type:", funcType)
            
        self.__interferenceRadius = self.findInterferenceRadius(3)
        
        self.__graph = graph
        self.__lineSegmInterferenceDict = self.constuctIncidenceVertexesGraph()


    def findInterferenceRadius(self, precision: int):
        factor = 1
        delta = 1
        resRadius = 0
        beginRadius = 1
        endRadius = 10
        for _ in range(precision):
            for radius in np.arange(beginRadius, endRadius, delta):
                lastP = self.__p(radius, self.__fVal)
                if lastP < 0.01:
                    resRadius = radius
                    beginRadius = resRadius - delta
                    endRadius = resRadius
                    delta = delta/10
                    break
        print("find radius", resRadius)
        print("val p", self.__p(resRadius, self.__fVal))
        resRadius = resRadius * factor
        return resRadius
    
    def getInterferenceRadius(self):
        return self.__interferenceRadius

    def constuctIncidenceVertexesGraph(self):
        print("construction of lineSegmInterferenceDict begin")
        edgeToNearPointIntervalDict = {}
        for firstNodeIndex in range(len(self.__graph)):
            secondNodeIndexList = self.__graph[firstNodeIndex]
            if secondNodeIndexList == []:
                continue
            for secondNodeIndex in secondNodeIndexList:
                if firstNodeIndex > secondNodeIndex:
                    continue

                edgeToNearPointIntervalDict[(firstNodeIndex, secondNodeIndex)] = []

        for edge in edgeToNearPointIntervalDict.keys():
            confIntervals = self.findConfidenceInterval(self.__pointList[edge[0]], self.__pointList[edge[1]], self.__pointList[edge[1]])
            intervalList = [edge[0], confIntervals[0], confIntervals[0]]
            edgeToNearPointIntervalDict[edge].append(intervalList)
            for nodeIndex in range(len(self.__pointList)):
                if nodeIndex == edge[0] or nodeIndex == edge[1]:
                    continue
                dist = Calculations.distLineSegm(self.__pointList[nodeIndex], self.__pointList[edge[0]], self.__pointList[edge[1]])
                if dist <= self.__interferenceRadius:
                    confIntervals = self.findConfidenceInterval(self.__pointList[edge[0]], self.__pointList[edge[1]], self.__pointList[nodeIndex])
                    intervalList = [nodeIndex, confIntervals[0], confIntervals[1]]
                    edgeToNearPointIntervalDict[edge].append(intervalList)

        print("construction of lineSegmInterferenceDict end")
        
        return edgeToNearPointIntervalDict
        
    def findConfidenceInterval(self, p_s, p_g, p_i):
        R = self.__interferenceRadius

        pr_0 = Calculations.distLineSegmParam(p_i, p_s, p_g)
        pr_1, pr_2 = Calculations.lineSegmCircleIntersectionParam(p_i, R, p_s, p_g)
        pr_s, pr_g = (0, 1)

        pr_a = max(pr_s, pr_1)
        pr_b = min(pr_g, pr_2)
        pr_c = Calculations.trunc(pr_0, pr_s, pr_g)
        
        dist_s_g = Calculations.dist(p_s, p_g)
        t_s_g = dist_s_g / R
        
        t_s_a = pr_a * t_s_g
        t_s_b = pr_b * t_s_g
        t_s_c = pr_c * t_s_g
        
        p_c = Calculations.interpolate(p_s, p_g, pr_c)
        dist_i_c = Calculations.dist(p_i, p_c)
        t_i_c = dist_i_c / R
        
        p_a = Calculations.interpolate(p_s, p_g, pr_a)
        dist_i_a = Calculations.dist(p_i, p_a)
        t_i_a = dist_i_a / R
        
        p_b = Calculations.interpolate(p_s, p_g, pr_b)
        dist_i_b = Calculations.dist(p_i, p_b)
        t_i_b = dist_i_b / R

        #for messages without length
        #dt_i_a = min(t_s_c - t_i_c, t_s_a - t_i_a)
        #dt_i_b = max(t_s_b - t_i_b, t_s_c - t_i_c)
        #
        #dt_i_b_ = t_s_g - max(t_s_c + t_i_c, t_s_b + t_i_b)
        #dt_i_a_ = t_s_g - min(t_s_c + t_i_c, t_s_a + t_i_a)
        
        #for messages with length
        t_d = self.__t_d

        dt_i_a = min(t_s_c - t_i_c - t_d, t_s_a - t_i_a - t_d)
        dt_i_b = max(t_s_b - t_i_b + t_d, t_s_c - t_i_c + t_d)
        
        dt_i_b_ = t_s_g - max(t_s_c + t_i_c + t_d, t_s_b + t_i_b + t_d)
        dt_i_a_ = t_s_g - min(t_s_c + t_i_c - t_d, t_s_a + t_i_a - t_d)
        
        res1 = [dt_i_a, dt_i_b]
        res2 = [dt_i_b_, dt_i_a_]
        
        if res1[0] > res1[1]:
            res1 = []
        if res2[0] > res2[1]:
            res2 = []
            
        return [res1, res2]
    
    def findConfidenceInterval2(self, p_s, p_g, p_i):
        R = self.__interferenceRadius
        dist_s_g = Calculations.dist(p_s, p_g)/R
        dist_i_g = Calculations.dist(p_i, p_g)/R
        dist_i_s = Calculations.dist(p_i, p_s)/R
        res1 = [dist_i_s+dist_s_g-dist_i_g]
    
    def findConflict(self, confInterval, t_s, t_i):
        dt_i = t_i - t_s
        hasConflict = False
        if Calculations.inInteval(dt_i, confInterval):
            hasConflict = True
        return hasConflict

    def processNode(self: ConflictHandler, nodePair: tuple[float, int], relevantHistory: list[Optional[tuple[float, int]]]):
        for index in range(len(relevantHistory)):
            ...

    def findSheduleConflict(self, INodeS: int, sTimes: list[float], tupleG: tuple[float, int]):
        INodeG = tupleG[1]
        gProcessTime = tupleG[0]
        edge = [INodeS, INodeG]
        edge.sort()


    #check conf3 whiht INodeG
    def findSheduleConflict(self, INodeS, INodeG, tNodeS):
        edge = [INodeS, INodeG]
        edge.sort()
        incidenceNodes = self.__lineSegmInterferenceDict[tuple(edge)]
        #incidenceNodes = []
        #for elem in self.__lineSegmInterferenceDict[tuple(edge)]:
        #    incidenceNodes.append(elem[0])
            
        incidenceNodes = list(list(zip(*self.__lineSegmInterferenceDict[tuple(edge)]))[0])
        #incidenceNodes2 = np.array(self.__lineSegmInterferenceDict[tuple(edge)])[:,1]
        #incidenceNodes.insert(0, INodeG)
        SColIndex = self.__colourList[INodeS]
        times = self.getColourTimes(SColIndex)
        t_s = 0
        p_s = self.__pointList[INodeS]
        p_g = self.__pointList[INodeG]

        

        globalHasConflict = False
        for index in range(len(incidenceNodes)):
            incNodeIndex = incidenceNodes[index]
            if index == 0:
                incNodeIndex = INodeG

            
            CurrColIndex = self.__colourList[incNodeIndex]
            #p_i = self.__pointList[incNodeIndex]
            t_i_list = times[CurrColIndex]
            #hasConflict = self.findConflict(p_s, p_g, p_i, t_s, t_i_list)
            #hasConflict2 = self.findConflict2(p_s, p_g, p_i, t_s, t_i_list)
            
            confIntervalList = self.__lineSegmInterferenceDict[tuple(edge)][index][1:]
            hasConflict3 = False
            if edge[0] == INodeS:
                confInterval = confIntervalList[0]
            else:
                confInterval = confIntervalList[1]
            for t_i in t_i_list:
                hasConflict3 = hasConflict3 or self.findConflict3(confInterval, t_s, t_i)
            #if hasConflict != hasConflict2:
            #    print("conflict1", INodeS, INodeG, incNodeIndex)
            #if hasConflict != hasConflict3:
            #    print("conflict2", INodeS, INodeG, incNodeIndex)
            globalHasConflict = globalHasConflict or hasConflict3
            

            
        return globalHasConflict
    
    def findSheduleConflict2(self, INodeS, INodeG, hyst_i, hyst_i_per):
        edge = [INodeS, INodeG]
        edge.sort()
        V_i_per = list(list(zip(*self.__lineSegmInterferenceDict[tuple(edge)]))[0])
        V_i_per[0] = INodeG
        
        p_s = self.__pointList[INodeS]
        p_g = self.__pointList[INodeG]

        probableSendTimes = []
        
        #t_i откуда, t_v_i_per - конфликтующий
        for t_i in hyst_i:
            totalHasConflict = False
            for index in range(len(V_i_per)):
                confIntervalList = self.__lineSegmInterferenceDict[tuple(edge)][index][1:]
                v_i_per = V_i_per[index]
                if edge[0] == INodeS:
                    confInterval = confIntervalList[0]
                else:
                    confInterval = confIntervalList[1]
                for t_v_i_per in hyst_i_per[v_i_per]:
                    hasConflict = self.findConflict3(confInterval, t_i, t_v_i_per)
                    totalHasConflict = totalHasConflict or hasConflict
                    if totalHasConflict:
                        break
                if totalHasConflict:
                    break
            if not totalHasConflict:
                probableSendTimes.append(t_i)
            else:
                ...
                #print("conflict")
        
        return probableSendTimes

    def findConflict3(self, confInterval, t_s, t_i):
        dt_i = t_i - t_s
        hasConflict = False
        if Calculations.inInteval(dt_i, confInterval):
            hasConflict = True
        return hasConflict
    
    def getConflictIndexes(self, INode_S, INode_G):
        edge = [INode_S, INode_G]
        edge.sort()
        V_i_per = list(list(zip(*self.__lineSegmInterferenceDict[tuple(edge)]))[0])
            
        return V_i_per