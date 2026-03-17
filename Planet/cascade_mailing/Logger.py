class Logger():
    def __init__(self, iNodeList, graph):
        self.number_of_attempts_to_send = [0 for _ in range(len(iNodeList))]
        self.total_num_of_sended_mes_coll_num = {}
        for i in range(len(iNodeList)):
            for j in graph[i]:
                self.total_num_of_sended_mes_coll_num[(i,j)] = [0, 0]
                
    def addAttemptToSend(self, iNodeList, num):
        for iNode in iNodeList:
            self.number_of_attempts_to_send[iNode] = self.number_of_attempts_to_send[iNode] + num
            
    def addCollisionsToNode(self, edgeBegEndList, num):
        for edgeBegEnd in edgeBegEndList:
            self.total_num_of_sended_mes_coll_num[edgeBegEnd][1] = self.total_num_of_sended_mes_coll_num[edgeBegEnd][1] + num
            
    def addTotalSendedToNode(self, edgeBegEndList, num):
        for edgeBegEnd in edgeBegEndList:
            self.total_num_of_sended_mes_coll_num[edgeBegEnd][0] = self.total_num_of_sended_mes_coll_num[edgeBegEnd][0] + num
            
    def reset(self, graph):
        self.number_of_attempts_to_send = [0 for _ in range(len(self.number_of_attempts_to_send ))]
        for i in range(len(graph)):
            for j in graph[i]:
                self.total_num_of_sended_mes_coll_num[(i,j)] = [0, 0]
        



