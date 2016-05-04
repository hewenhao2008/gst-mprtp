#!/bin/bash

function usage {
    echo "usage: $programname [options value]"
    echo "	--author 	determines the file used for reading the name of the author requested the tests"
    echo "	--srcdir 	determines the directory used as source of pdf plotfiles"
    echo "	--dst    	determines the path for the output"
    exit 1
}


if [  $# -le 1 ] 
then 
  usage
  exit 1
fi 

DST="report.tex"

while [[ $# > 1 ]]
do
key="$1"

case $key in
    --author)
    REPORTAUTHOR="$2"
    shift # past argument
    ;;
    --srcdir)
    SRCDIR="$2"
    shift # past argument
    ;;
    --dst)
    DST="$2"
    shift # past argument
    ;;
    --default)
    DEFAULT=YES
    ;;
    *)
    # unknown option
    ;;
esac
shift # past argument or value
done

echo "" > $DST

echo "\documentclass[a4paper]{article}

\usepackage[english]{babel}
\usepackage[utf8]{inputenc}
\usepackage{amsmath}
\usepackage{graphicx}
\usepackage[colorinlistoftodos]{todonotes}
\usepackage{pdfpages}
\usepackage{url}

\title{
ongested Feedback Link with Bi-directional RMCAT flows\\\[1cm]
\large \textbf{AUTOMATIC TEST REPORT}\\\[1cm]
}


\author{
Requested by 
AUTHORREPLACESTRING
}

\date{\today}

\begin{document}
\maketitle
" >> $DST

sed -e '/AUTHORREPLACESTRING/ {' -e 'r '$REPORTAUTHOR -e 'd' -e '}' -i $DST

echo "
\begin{abstract}
This report is generated by testscript found in\cite{mprtp_gstreamer_github}. It is intended to show results of tests performed on Multipath RTP Gstreamer plugin according to \cite{rmcat_test_draft}.
\end{abstract}

\section{Test Description}

RMCAT WG has been chartered to define algorithms for RTP hence it is
assumed that RTCP, RTP header extension or such would be used by the
congestion control algorithm in the backchannel.  Due to asymmetric
nature of the link between communicating peers it is possible for a
participating peer to not receive such feedback information due to an
impaired or congested backchannel (even when the forward channel
might not be impaired).  This test case is designed to observe the
candidate congestion control behaviour in such an event.\\

\textbf{Expected behavior}: It is expected that the candidate algorithms is able to cope with the
lack of feedback information and adapt to minimize the performance degradation of media flows in the forward channel. 

\begin{figure}
\begin{verbatim}
       		  +---+                                                         +---+
         |S1 |===== \                Forward -->              / =======|R1 |
         +---+      \\                                       //        +---+
                     \\                                     //
                  +-----+                               +-----+
                  |  A  |------------------------------>|  B  |
                  |     |<------------------------------|     |
                  +-----+                               +-----+
                     //                                     \\
                    //            <-- Backward               \\
        +---+      //                                         \\       +---+
        |R2 |===== /                                           \ ======|S2 |
        +---+                                                          +---+
\end{verbatim}
\caption{Testbed Topology for Congested Feedback Link}
\label{fig:ascii-box}
\end{figure}

" >> $DST

echo "
\pagebreak
\section{Results}

" >> $DST

RCVTHROUGHPUTSPLOT="$SRCDIR/rcv-throughputs.pdf"
SNDTHROUGHPUTSPLOT="$SRCDIR/snd-throughputs.pdf"
RCVTHROUGHPUTSPLOT="$SRCDIR/rcv-throughputs2.pdf"
SNDTHROUGHPUTSPLOT="$SRCDIR/snd-throughputs2.pdf"


echo "

\begin{figure}[h]
    \centering
    \includegraphics[width=1\textwidth]{$SNDTHROUGHPUTSPLOT}
    \caption{The measured throughputs for S1}
    \label{fig:sndthroughputs}
\end{figure}


\begin{figure}[h]
    \centering
    \includegraphics[width=1\textwidth]{$RCVTHROUGHPUTSPLOT}
    \caption{The measured throughputs at R1}
    \label{fig:rcvthroughputs}
\end{figure}

\begin{figure}[h]
    \centering
    \includegraphics[width=1\textwidth]{$SNDTHROUGHPUTSPLOT2}
    \caption{The measured throughputs for S2}
    \label{fig:sndthroughputs}
\end{figure}


\begin{figure}[h]
    \centering
    \includegraphics[width=1\textwidth]{$RCVTHROUGHPUTSPLOT2}
    \caption{The measured throughputs at R2}
    \label{fig:rcvthroughputs}
\end{figure}

" >> $DST


echo "

\clearpage
\begin{thebibliography}{9}
\bibitem{mprtp_gstreamer_github}Multipath RTP implementations, \url{https://github.com/multipath-rtp}

\bibitem{rmcat_test_draft}Test Cases for Evaluating RMCAT Proposals, \url{https://tools.ietf.org/html/draft-ietf-rmcat-eval-test-02}

\end{thebibliography}

% copyright note
{% begin group
   \vspace*{65mm}
   \thispagestyle{empty}
   \footnotesize\itshape
   \setlength{\parskip}{\baselineskip}
   \setlength{\parindent}{0pt}
   \copyright\,2016, by Balázs Kreith

   All Rights Reserved.
}% end group
\end{document}

" >> $DST




