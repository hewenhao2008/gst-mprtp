class PathConfig:
    """
    Represent a configuration for the path

    Attributes
    ----------
    bandwidth : int
        The bandwidth for the path [in kbps]
    latency : int
        The latency parameters for the forward path [in ms]
    jitter : int
        The jitter parameters for the forward path [in ms]
    queue_algorithm : QueueAlgorithms
        The queuing algorithm for the network
    """
    def __init__(self, bandwidth = 0, latency = 0, jitter = 0,  queue_algorithm = 0):
        """
        Init the parameter for the test

        Parameters
        ----------
        bandwidth : int
            The bandwidth parameter for the path
        latency : int
            The latency parameter for the path
        jitter : int
            The jitter parameters for the forward path
        queue_algorithm : QueueAlgorithms
            The queuing algorithm for the network
        """
        self.__bandwidth = bandwidth
        self.__latency = latency
        self.__jitter = jitter
        self.__queue_algorithm = queue_algorithm
        self.__burst = bandwidth / 10
        self.__min_burst = bandwidth / 10

    @property
    def bandwidth(self):
        """Gets the bandwidth"""
        return self.__bandwidth

    @property
    def jitter(self):
        """Gets the jitter"""
        return self.__jitter

    @property
    def latency(self):
        """Gets the forward_latency"""
        return self.__latency

    @property
    def burst(self):
        """Gets the burst"""
        return self.__burst

    @property
    def min_burst(self):
        """Gets the min_burst"""
        return self.__min_burst

    @property
    def queue_algorithm(self):
        """Gets the forward_latency"""
        return self.__queue_algorithm
