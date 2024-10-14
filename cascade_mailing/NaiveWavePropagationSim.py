from .Deployment import *
from .GraphView import *
from .TimeModel import *
from .SheduleHandler import *
from .ConcurentHandler import *
from .ConflictHandler import *
from .Logger import *
from .Settings import *
from .SimHandler import *
from .Storage import *

class NaiveWavePropagationSim():
    def __init__(self):
        self.iterSim()

        #self.gv.update(self.pointsWithMessage, self.targetPoints)

    def initNodesWithMessage(self, fractionLeft: float, fractionRight: float):
        xRightCoord = self.center[0] - self.sizeX/2 + self.sizeX*fractionLeft
        xLeftCoord = self.center[0] + self.sizeX/2 - self.sizeX*fractionRight
        for index in range(len(self.points)):
            if self.points[index][0] < xRightCoord:
                self.pointsWithMessage.append(index)
            if self.points[index][0] > xLeftCoord:
                self.targetPoints.append(index)

    def doOverlapTimeSim(self, rho):
        overlapList = []
        timeList = []
        fig, ax = plt.subplots()
        ax.set(xlabel="Пересечение по времени", ylabel="Время передачи", title="Плотность " + str(round(rho,1)))
        ax.plot(overlapList, timeList)
        name = str(round(rho,1))
        fig.savefig("randCascMailing/" + name + ".png")
        #plt.show()
        
    def sim(self):
        self.numberOfConflicts = 0
        self.numberOfSendedMessages = 0
        
        self.settings = Settings()
        self.storage = Storage()

        self.pg = PointsGenerator(self.settings.seed, self.settings.size, self.settings.rho, self.settings.pointGenType)
        self.storage.nodeCoords = self.pg.getPoints()

        self.eg = EdgeGenerator(self.storage.nodeCoords, self.settings.f_val, self.settings.relability, self.settings.probabilityFuncType, self.settings.calculateGraphMatrix)
        self.storage.edgesDict = self.eg.getEdgeDict()
        self.storage.keys = self.eg.getKeyList()
        self.storage.graph = self.eg.getGraph()

        self.tm = TimeModel(self.storage.nodeCoords)
        self.ch = ConflictHandler(self.storage.nodeCoords, self.settings.f_val, self.settings.probabilityFuncType, self.storage.graph)
        self.storage.interferenceRadius = self.ch.getInterferenceRadius()
        
        self.innerPoints = self.getNodesInRadius(self.storage.nodeCoords, self.storage.interferenceRadius, self.settings.center, self.settings.size)

        if self.settings.protocol == 0:
            self.sh = SheduleHandler(self.storage.nodeCoords, self.storage.interferenceRadius)
            self.currProtocot = self.sh
        if self.settings.protocol == 1:
            self.conh = ConcurentHandler(self.storage.nodeCoords)
            self.currProtocot = self.conh

            
        self.logger = Logger(self.storage.nodeCoords, self.storage.graph)
        
        max_time = 30
        
        self.meangraphPoints = [[],[]]
        
        self.simH = SimHandler(self.settings, self.storage, self.currProtocot, self.ch, self.tm, self.logger, max_time)
        
        print("here")
        
        self.gv = GraphView(self.storage.nodeCoords, self.storage.edgesDict, self.storage.keys, self.storage.nodeCoords, self.settings.range, self.storage.nodesWithMessage, self.storage.targetNodes)
        
        #plt.show()
        
    def algStep(self, max_time):
        currNodesToProcess = self.tm.getNodesToProcess()
        #nextProcessList = self.processNodes(currNodesToProcess)
        self.__currentTime = currNodesToProcess[0]
        currProcessList = currNodesToProcess[1]
        self.updateNodesWithMessages(currProcessList)
        #sendList, nextProcessList = self.sh.processNodes(currProcessList, self.pointsWithMessage, self.__currentTime)
        if self.settings.protocol == 1:
            interval, mesWantToHear = self.conh.getNodesWhichWantToHear(currProcessList, self.pointsWithMessage, self.__currentTime)
            nodesThatHear = self.didNodesHearSomething(mesWantToHear, interval[0], interval[1])
            self.conh.updateStateList(mesWantToHear, nodesThatHear)
        if self.settings.protocol == 0:
            sendList, nextProcessList = self.sh.processNodes(currProcessList, self.pointsWithMessage, self.__currentTime)
        if self.settings.protocol == 1:
            sendList, nextProcessList = self.conh.processNodes(currProcessList, self.pointsWithMessage, self.__currentTime)
        self.tm.updateLastProcessTimes(currProcessList, self.__currentTime)
        if sendList != []:
            self.sendMessages(self.__currentTime, sendList)
        #print(self.__currentTime)
        if not self.settings.loadTest:    
            if set(self.pointsWithMessage).intersection(self.targetPoints) != set():
                print("point with message", set(self.pointsWithMessage).intersection(self.targetPoints))
                return False
        self.addNodesToProcess(nextProcessList)
        if self.tm.getCurrentTime() > max_time:
            return False
        return True

    def processNodes(self, processList: list[tuple[float, int]]):
        
        self.__currentTime, currProcessList = processList
        self.updateNodesWithMessages(currProcessList)
        #hear messages
        if self.settings.protocol == 1:
            interval, mesWantToHear = self.conh.getNodesWhichWantToHear(currProcessList, self.pointsWithMessage, self.__currentTime)
            nodesThatHear = self.didNodesHearSomething(mesWantToHear, interval[0], interval[1])
            self.conh.updateStateList(mesWantToHear, nodesThatHear)
        if self.settings.protocol == 0:
            sendList, nextProcessList = self.sh.processNodes(currProcessList, self.pointsWithMessage, self.__currentTime)
        if self.settings.protocol == 1:
            sendList, nextProcessList = self.conh.processNodes(currProcessList, self.pointsWithMessage, self.__currentTime)
        self.tm.updateLastProcessTimes(currProcessList, self.__currentTime)
        if sendList != []:
            self.sendMessages(self.__currentTime, sendList)
        print("here")
        return nextProcessList
                
    
    
    def collectHistoryByTick(self):
        history = self.tm.getSendedHistory()
        sended = [[] for _ in range(len(self.storage.nodeCoords))]
        for iNode in range(len(self.storage.nodeCoords)):
            incNodes = self.storage.graph[iNode]
            for incNode in incNodes:
                incNodeDist = Calculations.dist(self.storage.nodeCoords[iNode], self.storage.nodeCoords[incNode])
                newIncHistList = [x + incNodeDist for x in history[incNode]]
                sended[iNode] = sended[iNode] + newIncHistList
            sended[iNode].sort()
        return sended
    
    def getNodesInRadius(self, pointList, radius, center_coord, size):
        indexVericiesIn = []
        for index, point in enumerate(pointList):
            hor_cond = center_coord[0] - size[0]/2 + radius <= point[0]  <= center_coord[0] + size[0]/2 - radius
            ver_cond = center_coord[1] - size[1]/2 + radius <= point[1]  <= center_coord[1] + size[1]/2 - radius
            if hor_cond and ver_cond:
                indexVericiesIn.append(index)
        return indexVericiesIn
    
    def filterByIndexList(self, filterList, indexList):
        res = []
        for index in indexList:
            res.append(filterList[index])
        return res
    
    def iterSim(self):
        self.initSim()
        while True:
            self.updateSimParameters()
            self.doSim()
            self.handleResults()
            if self.settings.rho >= 3:
                break
        self.viewRes()
        print("here")
        
    def viewRes(self):
        fig, ax = plt.subplots()
        ax.set(xlabel="Плотность", ylabel="Средняя вероятность пустоты слота", title="res")
        ax.plot(self.meangraphPoints[0], self.meangraphPoints[1])
        name = "res"
        #fig.savefig("randCascMailing/" + name + ".png")
        plt.show()
            
    def initSim(self):
        
        self.settings = Settings()
        self.storage = Storage()

        #self.pg = PointsGenerator(self.settings.seed, self.settings.size, self.settings.rho, self.settings.pointGenType)
        #self.storage.nodeCoords = self.pg.getPoints()
        #
        #self.eg = EdgeGenerator(self.storage.nodeCoords, self.settings.f_val, self.settings.relability, self.settings.probabilityFuncType, self.settings.calculateGraphMatrix)
        #self.storage.edgesDict = self.eg.getEdgeDict()
        #self.storage.keys = self.eg.getKeyList()
        #self.storage.graph = self.eg.getGraph()
        #
        #self.tm = TimeModel(self.storage.nodeCoords)
        #self.ch = ConflictHandler(self.storage.nodeCoords, self.settings.f_val, self.settings.probabilityFuncType, self.storage.graph)
        #self.storage.interferenceRadius = self.ch.getInterferenceRadius()
        #
        #self.innerPoints = self.getNodesInRadius(self.storage.nodeCoords, self.storage.interferenceRadius, self.settings.center, self.settings.size)
        #
        #if self.settings.protocol == 0:
        #    self.sh = SheduleHandler(self.storage.nodeCoords, self.storage.interferenceRadius)
        #    self.currProtocot = self.sh
        #if self.settings.protocol == 1:
        #    self.conh = ConcurentHandler(self.storage.nodeCoords)
        #    self.currProtocot = self.conh
        #
        #    
        #self.logger = Logger(self.storage.nodeCoords, self.storage.graph)
        #
        #max_time = 10
        #
        self.meangraphPoints = [[],[]]
        
        #self.simH = SimHandler(self.settings, self.storage, self.currProtocot, self.ch, self.tm, self.logger, max_time)
        
    def updateSimParameters(self):
        self.settings.rho = self.settings.rho + 0.25
        

        self.pg = PointsGenerator(self.settings.seed, self.settings.size, self.settings.rho, self.settings.pointGenType)
        self.storage.nodeCoords = self.pg.getPoints()
        #self.storage.nodeCoords = self.pg.testGeneration()

        self.eg = EdgeGenerator(self.storage.nodeCoords, self.settings.f_val, self.settings.relability, self.settings.probabilityFuncType, self.settings.calculateGraphMatrix)
        self.storage.edgesDict = self.eg.getEdgeDict()
        self.storage.keys = self.eg.getKeyList()
        self.storage.graph = self.eg.getGraph()

        self.tm = TimeModel(self.storage.nodeCoords)
        self.ch = ConflictHandler(self.storage.nodeCoords, self.settings.f_val, self.settings.probabilityFuncType, self.storage.graph)
        self.storage.interferenceRadius = self.ch.getInterferenceRadius()
        
        self.innerPoints = self.getNodesInRadius(self.storage.nodeCoords, self.storage.interferenceRadius, self.settings.center, self.settings.size)
        
        if self.settings.protocol == 0:
            self.sh = SheduleHandler(self.storage.nodeCoords, self.storage.interferenceRadius)
            self.currProtocot = self.sh
        if self.settings.protocol == 1:
            self.conh = ConcurentHandler(self.storage.nodeCoords)
            self.currProtocot = self.conh

            
        self.logger = Logger(self.storage.nodeCoords, self.storage.graph)

        max_time = 200

        self.simH = SimHandler(self.settings, self.storage, self.currProtocot, self.ch, self.tm, self.logger, max_time)

        #self.simH.tm = self.tm
        #self.simH.ch = self.ch
        #self.simH.storage = self.storage
        #self.simH.protocol = self.currProtocot
        #self.simH.logger = self.logger
        #
        #self.simH.resetSim()
        
        
    def doSim(self):
        self.simH.doSim()
        
    def handleResults(self):
        y = self.averageRes()
        x = self.settings.rho
        print("rho", x, "average", y)
        self.meangraphPoints[0].append(x)
        self.meangraphPoints[1].append(y)
        probList, prob = self.getInnerEdges()
        res = np.histogram(probList)
        print(res)
        plt.hist(probList)  # density=False would make counts
        plt.ylabel('Probability')
        plt.xlabel('Data');
        plt.show()
        print("here")
        
    def averageRes(self):
        self.innerPoints = self.getNodesInRadius(self.storage.nodeCoords, self.storage.interferenceRadius, self.settings.center, self.settings.size)
        res1 = self.collectHistoryByTick()
        res2 = self.conh.getFreeTicksList(res1, self.tm.getCurrentTime())
        res3 = self.conh.func_cnt(res2, 1)
        res4 = self.filterByIndexList(res3, self.innerPoints)
        y = np.mean(res4)
        return y

    def getInnerEdges(self):
        data = self.logger.total_num_of_sended_mes_coll_num
        innerSet = set(self.innerPoints)
        res = [0, 0]
        resList = []
        for key in data.keys():
            if key[0] in innerSet and key[1] in innerSet:
                element = data[key]
                resList.append(element[1]/element[0])
                res[0] = res[0] + element[0]
                res[1] = res[1] + element[1]
                
        prob = res[1]/res[0]
                
        return resList, prob
                
        
        