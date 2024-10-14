class Storage:
    def __init__(self):
        self.nodeCoords = []
        
        #sorted index pair -> p edge val
        self.edgesDict = {}
        #list of sorted pairs
        self.keys = []
        #connectivity list
        self.graph = []
        
        self.incidenceNatrix = []
        
        #indicies
        self.nodesWithMessage = []
        self.targetNodes = []
        
        #max radius of signal
        self.interferenceRadius = 0
        
        #for statistics
        self.innerPoints = []
        





