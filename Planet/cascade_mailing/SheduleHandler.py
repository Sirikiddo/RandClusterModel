from __future__ import annotations
from typing import Optional
from .Deployment import Calculations
from .TimeModel import *

class SheduleHandler:
    def __init__(self: SheduleHandler, pointList: list[list[float]], interferenceRadius: float):
        #coloring nodes
        self.__interferenceRadius = interferenceRadius
         
        self.__edgeDict = {}
        self.__keyList = []

        self.__interferenceGraph = [[] for _ in range(len(pointList))]

        self.__colourList = [-1] * len(pointList)
        
        self.produceInterferenceGtaph(pointList, self.__interferenceRadius)
        
        self.graphColouring()

        self.__maxValence = self.getMaxValence()
        self.__maxColour = self.calculateMaxColour()

        print("max valence", self.__maxValence)
        print("max colour", self.__maxColour)

        self.__nodesSortedByColours = [[] for _ in range(self.__maxColour + 1)]

        self.sortNodesByColour()
        

        #shedule handler
        #time in dimensionless quantities
        self.__slotLength = 1
        self.__slotOverLap = 0.0
        
        
    def produceInterferenceGtaph(self: SheduleHandler, pointList, interferenceRadius):

        for i in range(len(pointList)):
            for j in range(i + 1, len(pointList)):
                dist = Calculations.dist(pointList[i], pointList[j])
                if dist < interferenceRadius:
                    self.__edgeDict[(i,j)] = 1
                    self.__keyList.append((i,j))
                    self.__interferenceGraph[i].append(j)
                    self.__interferenceGraph[j].append(i)
        
    def graphColouring(self: SheduleHandler):
        V = len(self.__interferenceGraph)
        self.__colourList

        # Assign the first color to first vertex
        self.__colourList[0] = 0

        # A temporary array to store the available colors. 
        # True value of available[cr] would mean that the
        # color cr is assigned to one of its adjacent vertices
        available = [False] * V

        # Assign colors to remaining V-1 vertices
        for u in range(1, V):
         
            # Process all adjacent vertices and
            # flag their colors as unavailable
            for i in self.__interferenceGraph[u]:
                if (self.__colourList[i] != -1):
                    available[self.__colourList[i]] = True
 
            # Find the first available color
            cr = 0
            while cr < V:
                if (available[cr] == False):
                    break
             
                cr += 1
             
            # Assign the found color
            self.__colourList[u] = cr 
 
            # Reset the values back to false 
            # for the next iteration
            for i in self.__interferenceGraph[u]:
                if (self.__colourList[i] != -1):
                    available[self.__colourList[i]] = False
                    
    def sortNodesByColour(self: SheduleHandler):
        for index in range(len(self.__colourList)):
            self.__nodesSortedByColours[self.__colourList[index]].append(index)
                
    def getColourList(self: SheduleHandler):
        return self.__colourList

    def getEdgeDict(self: SheduleHandler):
        return self.__edgeDict

    def getKeyList(self: SheduleHandler):
        return self.__keyList

    def getMaxValence(self: SheduleHandler):
        maxValence = 0
        for node in self.__interferenceGraph:
            if maxValence < len(node):
                maxValence = len(node)
        return maxValence

    def calculateMaxColour(self: SheduleHandler) -> int:
        maxColour: int = 0
        for colour in self.__colourList:
            if maxColour < colour:
                maxColour = colour
        return maxColour

    def getNodesSortedByColour(self: SheduleHandler) -> list[tuple[float, int]]:
        return self.__nodesSortedByColours

    def getInterferenceGraph(self: SheduleHandler) -> list[list[int]]:
        return self.__interferenceGraph

    def getInterferenceRadius(self: SheduleHandler) -> float:
        return self.__interferenceRadius
    
    def allNodesFirstProcessLists(self, currentTime: float):
        listTimeListPairs = []
        time = currentTime
        realTimeStep: float = self.__slotLength - self.__slotOverLap
        for colour in range(self.__maxColour + 1):
            listTimeListPairs.append((time, self.__nodesSortedByColours[colour]))
            time = time + realTimeStep
            
        return listTimeListPairs
    
    def sendMessage(self, iNode):
        return True
        
    def processNodes(self, iNodeList: list[int], nodesWithMessage: list[int], currentTime: float):
        sendNodes = list(set(iNodeList).intersection(nodesWithMessage))
        processList = [(self.nextCycleTime(currentTime), iNodeList)]
        res = (sendNodes, processList)
        return res
    
    def reset(self):
        ...