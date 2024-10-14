from __future__ import annotations
from scipy.stats import qmc
import scipy.special as scpy
import numpy as np
import math
import functools

class PointsGenerator:
    def __init__(self, seed: int, sym_size: list[float], rho: float, pointGenType: int) -> None:
        # Параметры
        self.__seed = seed # Сид
        
        self.__rho = rho # Плотность
        self.__width, self.__height = sym_size[0], sym_size[1] # Ширина и высота прямоугольника
        

        self.__pointGenType = pointGenType # Способ генерации
        # 0 - псевдослучайная генерация
        # 1 - точки на сетке
        # 2 - квазислучайная генерация, метод Собеля
        # 3 - квазислучайная генерация, метод Халтона

        self.__points = self.__generatePoints()

    def __generatePoints(self) -> list[float]:
        # Определение параметра n как функции от rho
        area = self.__height * self.__width
        n_sample = int(area * self.__rho)  # Примерное количество точек
        n_sample_corrected = 2**np.ceil(np.log2(n_sample)).astype(int)

        centerPoint = (0, 0)  # Центр прямоугольника

        rng = np.random.default_rng(self.__seed) # Задание сида для генератора случайнах чисел

        points = np.array([])

        # Генерация точек
        if self.__pointGenType == 0:
            points = np.array([(self.__width * rng.random() - self.__width / 2, self.__height * rng.random() - self.__height / 2) for _ in range(n_sample)])

        # Для данного метода пока нет смещения от центра
        plist = []
        if self.__pointGenType == 1:
            d = 1 / math.sqrt(self.__rho)
            x = - int(self.__width / 2 / d) * d
            y = int(self.__height / 2 / d) * d
            for j in range(math.ceil(self.__height / d)):
                for i in range(math.ceil(self.__width / d)):
                    plist.append((x + i * d, y - j * d))
            points = np.array(plist)

        if self.__pointGenType == 2:
            engine_sobol = qmc.Sobol(d=2, seed=rng)
            points_sobol = engine_sobol.random(n_sample_corrected)
            points = points_sobol * np.array([self.__width, self.__height]) + np.array([centerPoint[0] - self.__width / 2, centerPoint[1] - self.__height / 2])

        if self.__pointGenType == 3:
            engine_halton = qmc.Halton(d=2, seed=rng)
            points_halton = engine_halton.random(n_sample)
            points = points_halton * np.array([self.__width, self.__height]) + np.array([centerPoint[0] - self.__width / 2, centerPoint[1] - self.__height / 2])

        return points.tolist()
    
    def testGeneration(self):
        points = []
        points.append([0,0])
        points.append([0.85,0])
        return points

    def getPoints(self) -> list[float]:
        return self.__points

class Calculations:
    vbit = 10
    pn = 6.71
    
    #@functools.cache
    @staticmethod
    def fromUnitToSec(unit):
        return unit*2.29333
    
    #@functools.cache
    @staticmethod
    def fromSecToUnit(sec):
        return sec*0.436047
    
    #@functools.cache
    @staticmethod
    def interpolate(p_0, p_1, param):
        res = [p_0[0]*(1-param) + p_1[0]*param, p_0[1]*(1-param) + p_1[1]*param]
        return res
    
    #@functools.cache
    @staticmethod
    def trunc(val, a, b):
        if val < a:
            return a
        if val > b:
            return b
        return val

    #@functools.cache
    @staticmethod
    def dist(p1: list[float], p2: list[float]):
        return np.linalg.norm(np.absolute(np.array(p1) - np.array(p2)))

    #@functools.cache
    @staticmethod
    def distLineSegm(p0, p1, p2):
        A = p0[0] - p1[0]
        B = p0[1] - p1[1]
        C = p2[0] - p1[0]
        D = p2[1] - p1[1]

        dot = A * C + B * D
        len_sq = C * C + D * D
        param = -1;
        if len_sq != 0: #in case of 0 length line
            param = dot / len_sq

        if param < 0:
            xx = p1[0]
            yy = p1[1]
        elif param > 1:
            xx = p2[0]
            yy = p2[1]
        else:
            xx = p1[0] + param * C
            yy = p1[1] + param * D

        dx = p0[0] - xx
        dy = p0[1] - yy
        return math.sqrt(dx * dx + dy * dy)

    #@functools.cache
    @staticmethod
    def distLineSegmParam(p0, p1, p2):
        A = p0[0] - p1[0]
        B = p0[1] - p1[1]
        C = p2[0] - p1[0]
        D = p2[1] - p1[1]

        dot = A * C + B * D
        len_sq = C * C + D * D
        param = math.inf;
        if len_sq != 0: #in case of 0 length line
            param = dot / len_sq
        return param
    
    #@functools.cache
    @staticmethod
    def LineSegmCircleIntersection(p0, R, p1, p2):
        x0, y0 = p0[0], p0[1]
        x1, y1 = p1[0], p1[1]
        x2, y2 = p2[0], p2[1]
        D = (R**2)*((x1-x2)**2+(y1-y2)**2)-(x2*(y1-y0)+x1*(y0-y2)+x0*(y2-y1))**2
        if D<0:
            return ()
        A = (x1-x2)**2+(y1-y2)**2
        B = (x0-x2)*(x1-x2)+(y0-y2)*(y1-y2)
        t1 = (B-math.sqrt(D))/A
        t2 = (B+math.sqrt(D))/A
        M = [x2-x1,y2-y1]
        PT1 = [x0 + M[0]*t1, y0 + M[1]*t1]
        if D ==0:
            return (PT1)
        PT2 = [x0 + M[0]*t2, y0 + M[1]*t2]
        return (PT1, PT2)
    
    #@functools.cache
    @staticmethod
    def lineSegmCircleIntersectionParam(p0, R, p1, p2):
        x0, y0 = p0[0], p0[1]
        x1, y1 = p1[0], p1[1]
        x2, y2 = p2[0], p2[1]
        #print((R**2)*((x1-x2)**2+(y1-y2)**2))
        #print((x2*(y1-y0)+x1*(y0-y2)+x0(y2-y1))**2)
        D = (R**2)*((x1-x2)**2+(y1-y2)**2)-(x2*(y1-y0)+x1*(y0-y2)+x0*(y2-y1))**2
        if D<0:
            return ()
        A = (x1-x2)**2+(y1-y2)**2
        B = (x1-x0)*(x1-x2)+(y1-y0)*(y1-y2)
        t1 = (B-math.sqrt(D))/A
        t2 = (B+math.sqrt(D))/A
        #if D ==0:
        #    return (t1)
        return (t1, t2)

    #@functools.cache
    @staticmethod
    def isOverlap(interval1, interval2):
        res = interval1[0] <= interval2[1] and interval2[0] <= interval1[1]
        return res
    
    #@functools.cache
    @staticmethod
    def intervalInInterval(interval1, interval2):
        res = interval1[0] >= interval2[0] and interval1[1] <= interval2[1]
        return res

    #@functools.cache
    @staticmethod
    def inInteval(t, interval):
        res = t>=interval[0] and t<=interval[1]
        return res

    #@functools.cache
    @staticmethod
    def x(r: float, f: float) -> float:
        x = (np.sqrt(f / Calculations.vbit) * (Calculations.pn)) / (r) * 10 ** (-0.05 * Calculations.beta(f) * r)
        #print('x - ', x)
        return x

    #@functools.cache
    @staticmethod
    def beta(f: float) -> float:
        f = (0.1 * f**2/(1 + f**2)) + (40 * f**2 / (4100 + f**2)) + (2.75 * 10**(-4) * f**2) + 0.0003
        #print('beta - ', f)
        return f

    #@functools.cache
    @staticmethod
    def p1(r: float, f: float) -> float:
        p1 = scpy.erf(Calculations.x(r, f))
        #print('p2 - ', p1)
        return p1
    
    #@functools.cache
    @staticmethod
    def qe(r: float, f: float) -> float:
        qe = 0.5 * (1 - math.sqrt(Calculations.gamma(r,f) / (1 + Calculations.gamma(r, f))))
        #print('qe - ', qe)
        return qe

    #@functools.cache
    @staticmethod
    def gamma(r: float, f: float) -> float:
        gamma = (f * 10**2) / (r**2) * 10**(-0.1 * Calculations.beta(f) * r)
        #print('gamma - ', gamma)
        return gamma

    #@functools.cache
    @staticmethod
    def p2(r: float, f: float) -> float:
        p2 = (1 - Calculations.qe(r, f))**256
        #print('p2 - ', p2)
        return p2

class EdgeGenerator:
    def __init__(self, pointList: list[list[float]], fVal: float, reliability: float, funcType: int, computePMatrix: bool) -> None:
        self.__P = [[0 for _ in range(len(pointList))] for _ in range(len(pointList))]
        self.__edgeDict = {}
        self.__keyList = []
        self.__graph = [[] for _ in range(len(pointList))]
        self.__fVal = fVal

        self.__computePMatrix = computePMatrix

        if funcType == 1:
            self.p = np.vectorize(Calculations.p1)
        elif funcType == 2:
            self.p = np.vectorize(Calculations.p2)
        else:
            print("incorrect probability function type:", funcType)

        for i in range(len(pointList)):
            for j in range(i + 1, len(pointList)):
                dist = Calculations.dist(pointList[i], pointList[j])
                curr_p = self.p(dist, self.__fVal)
                if curr_p > reliability:
                    self.__edgeDict[(i,j)] = curr_p
                    self.__keyList.append((i,j))
                    self.__graph[i].append(j)
                    self.__graph[j].append(i)
                    if self.__computePMatrix:
                        self.__P[i][j] = curr_p
                        self.__P[j][i] = curr_p
                        
        #print(self.__P)

    def getEdgeDict(self):
        return self.__edgeDict

    def getKeyList(self):
        return self.__keyList

    def getPMatrix(self):
        if not self.__computePMatrix:
            print("P matrix is empty")

        return self.__P

    def getGraph(self):
        return self.__graph