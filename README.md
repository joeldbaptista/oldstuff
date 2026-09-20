# A repo to keep old code

In this repo I want to keep all my old code. There is no garantee the code is correct.

## SVM

In `svm` is some of the code I use in my [PhD thesis](http://hdl.handle.net/10362/33031).
I've made some changes; nothing critical: I added more comments, and reformatted the code.

This code was based on [libsvm](https://www.csie.ntu.edu.tw/~cjlin/libsvm/index.html).
During my PhD studies I have relied a lot in [this](https://www.amazon.co.uk/Support-Vector-Machines-Optimization-Algorithms/dp/143985792X/ref=sr_1_4?crid=29N1OI1DUNEK4&dib=eyJ2IjoiMSJ9.n4awtKukFNbUmIKlJ_KRNCLmx_uVobESjde4AISI3YmmvpixAHReMiE0MzpdDR0iCAh0pecXYyFuRP4_XZ8_sXfklHJlEQImpjh_9Nn_Kc4.QwZJkw23kOQ8YdeUdZyasKeCWwh6bfXJ4GqqloDAyig&dib_tag=se&keywords=svm+support+vector+machines&qid=1789939688&sprefix=svm+support+vector+machine%2Caps%2C146&sr=8-4)
as well. This pair is, in my opinion, the two sources to really learn how SVMs work.

The main objective of writing this code was to create a setup so tests could be easily run,
and also I could really learn how SVMs work internally. The current version was compared
with the recent versions of LibSVM, and shows no deviations.

## Tree sandbox

During my PhD I wanted to explore what could be done with trees. For example, how to use
tree dept and space partition of find high density regions of the feature space. I would 
like to have had the time to explore it more, but that has not happened. 

With `itrees` I try to experimentally use the idea of isolation forest to find thematic
outlines. Did not work as expected. 
